// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "OnlineSACLearner.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace online_sac {
using namespace detail;
namespace {
using LearnerClock = std::chrono::steady_clock;

// RLtools types for each instantiated observation size. Chase v1 keeps its original environment type
// so its SAC state (and therefore its results) is unchanged; chase v2 uses the spaces-only environment.
template <unsigned O> struct LearnerEnvironment;
template <> struct LearnerEnvironment<ObservationDim> { using Type = ChaseEnv; };
template <> struct LearnerEnvironment<ChaseObservationSize> { using Type = rlt::rl::environments::Spaces<T, TI, ChaseObservationSize, ChaseActionSize, 300>; };
template <unsigned O>
struct LearnerTypes {
    using Env = typename LearnerEnvironment<O>::Type;
    using State = SACState<Env>;
    using InferenceActor = typename SACConfig<Env>::EVAL_ACTOR_TYPE::template CHANGE_CAPABILITY<rlt::nn::capability::Forward<true>>;
};

template <unsigned O>
struct Experience {
    float observation[O], action[ActionDim], nextObservation[O];
    float reward;
    bool terminated, truncated;
};
bool Finite(const float* values, unsigned count) {
    for (unsigned i = 0; i < count; ++i) if (!std::isfinite(values[i])) return false;
    return true;
}
}

template <unsigned O>
struct BasicOnlineSACLearner<O>::Policy {
    Device device;
    typename LearnerTypes<O>::InferenceActor actor;
    unsigned version;
    template <typename SOURCE>
    Policy(Device& sourceDevice, SOURCE& source, unsigned number) : version(number) {
        rlt::malloc(device, actor);
        rlt::copy(sourceDevice, device, source, actor);
    }
    ~Policy() { rlt::free(device, actor); }
    // Writes the actor output for `observation`; samples when `rollout`, else the mean.
    void Evaluate(const float* observation, float* action, Rng& rng, bool rollout) const {
        Device local;
        RowTensor<O> input;
        RowTensor<ActionDim> output;
        typename LearnerTypes<O>::InferenceActor::template Buffer<true> buffer;
        rlt::malloc(local, buffer);
        for (TI i = 0; i < O; ++i) rlt::set(local, input, observation[i], 0, 0, i);
        if (rollout) rlt::evaluate(local, actor, input, output, buffer, rng, rlt::Mode<rlt::mode::Rollout<>>{});
        else rlt::evaluate(local, actor, input, output, buffer, rng, rlt::Mode<rlt::mode::Evaluation<>>{});
        for (TI i = 0; i < ActionDim; ++i) action[i] = rlt::get(local, output, 0, 0, i);
        rlt::free(local, buffer);
    }
};

template <unsigned O>
struct BasicOnlineSACLearner<O>::Impl {
    using State = typename LearnerTypes<O>::State;
    using Env = typename LearnerTypes<O>::Env;
    OnlineSACConfig config;
    Device device;
    std::unique_ptr<State> ts;
    std::thread thread;

    mutable std::mutex mutex;
    std::condition_variable wake, roundDone;
    std::deque<Experience<O>> queue;
    OnlineSACStats stats;
    uint64_t covered = 0;
    bool stop = false;
    std::atomic<bool> stopFlag{false}, frozen{false};

    mutable std::mutex snapshotMutex;
    std::shared_ptr<const Policy> snapshot;
    mutable std::atomic<uint64_t> actCalls{0};

    explicit Impl(const OnlineSACConfig& value) : config(value) {
        if (config.updateEvery == 0) config.updateEvery = 1;
        if (config.queueCapacity == 0) config.queueCapacity = 1;
        frozen = config.frozen;
        stats.frozen = config.frozen;
        ts = std::make_unique<State>();
        rlt::malloc(device, *ts);
        rlt::init(device, *ts, config.seed);
        snapshot = std::make_shared<const Policy>(device, ts->actor_critic.actor, 0u);
        stats.running = true;
        thread = std::thread([this] { Run(); });
    }
    ~Impl() {
        StopAndJoin();
        rlt::free(device, *ts);
    }
    void StopAndJoin() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
            stopFlag = true;
        }
        wake.notify_all();
        roundDone.notify_all();
        if (thread.joinable()) thread.join();
        std::lock_guard<std::mutex> lock(mutex);
        stats.running = false;
        stats.training = false;
    }
    void Store(const Experience<O>& e) {
        auto& buffer = rlt::get(ts->off_policy_runner.replay_buffers, 0, 0);
        rlt::Matrix<rlt::matrix::Specification<T, TI, 1, O, false>> observation, next;
        rlt::Matrix<rlt::matrix::Specification<T, TI, 1, ActionDim, false>> action;
        for (TI i = 0; i < O; ++i) { rlt::set(observation, 0, i, e.observation[i]); rlt::set(next, 0, i, e.nextObservation[i]); }
        for (TI i = 0; i < ActionDim; ++i) rlt::set(action, 0, i, e.action[i]);
        const typename Env::State unusedState{};
        rlt::add(device, buffer, unusedState, observation, observation, action, e.reward, unusedState, next, next, e.terminated, e.truncated || e.terminated);
    }
    void Update() {
        State& s = *ts;
        rlt::gather_batch(device, s.off_policy_runner, s.critic_batch, s.rng);
        rlt::randn(device, s.action_noise_critic, s.rng);
        for (int critic = 0; critic < 2; ++critic) {
            rlt::train_critic(device, s.actor_critic, s.actor_critic.critics[critic], s.critic_batch, s.actor_critic.critic_optimizers[critic],
                s.actor_target_buffers[critic], s.critic_buffers[critic], s.critic_target_buffers[critic], s.critic_training_buffers[critic], s.action_noise_critic, s.rng);
        }
        rlt::update_critic_targets(device, s.actor_critic);
        rlt::randn(device, s.action_noise_actor, s.rng);
        rlt::train_actor(device, s.actor_critic, s.critic_batch, s.actor_critic.actor_optimizer, s.actor_buffers[0], s.critic_buffers[0], s.actor_training_buffers, s.action_noise_actor, s.rng);
    }
    void Run() {
        std::vector<Experience<O>> batch;
        uint64_t pending = 0, storedTotal = 0;
        unsigned version = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stop || !queue.empty(); });
                if (stop) return;
                batch.assign(queue.begin(), queue.end());
                queue.clear();
                stats.queued = 0;
            }
            for (const Experience<O>& e : batch) Store(e);
            storedTotal += batch.size();
            pending += batch.size();
            {
                std::lock_guard<std::mutex> lock(mutex);
                stats.stored = storedTotal;
                if (pending < config.updateEvery) continue;
                stats.training = true;
            }
            const auto start = LearnerClock::now();
            const uint64_t updates = (!frozen && storedTotal >= config.warmupExperiences) ? pending : 0;
            uint64_t done = 0;
            for (; done < updates && !stopFlag; ++done) Update();
            if (stopFlag) return;
            if (updates > 0) {
                auto next = std::make_shared<const Policy>(device, ts->actor_critic.actor, ++version);
                std::lock_guard<std::mutex> lock(snapshotMutex);
                snapshot = std::move(next);
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                stats.updates += done;
                stats.version = version;
                stats.rounds += 1;
                stats.training = false;
                stats.lastRoundMs = std::chrono::duration<double, std::milli>(LearnerClock::now() - start).count();
                covered += pending;
            }
            pending = 0;
            roundDone.notify_all();
        }
    }
    std::shared_ptr<const Policy> Current() const {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        return snapshot;
    }
};

template <unsigned O>
BasicOnlineSACLearner<O>::BasicOnlineSACLearner(const OnlineSACConfig& config) : data(std::make_unique<Impl>(config)) {}
template <unsigned O>
BasicOnlineSACLearner<O>::~BasicOnlineSACLearner() = default;

template <unsigned O>
AddResult BasicOnlineSACLearner<O>::AddExperience(const float* observation, unsigned observationSize, const float* action, unsigned actionSize, float reward,
    const float* nextObservation, unsigned nextObservationSize, bool terminated, bool truncated) {
    const bool valid = observation && action && nextObservation
        && observationSize == O && actionSize == ActionDim && nextObservationSize == O
        && Finite(observation, O) && Finite(nextObservation, O) && Finite(action, ActionDim) && std::isfinite(reward)
        && std::fabs(action[0]) <= 1.0f && std::fabs(action[1]) <= 1.0f;
    std::lock_guard<std::mutex> lock(data->mutex);
    if (!valid) { ++data->stats.rejected; return AddResult::Rejected; }
    if (data->frozen) { ++data->stats.ignoredFrozen; return AddResult::Frozen; }
    if (data->stop) { ++data->stats.dropped; return AddResult::Dropped; }
    if (data->queue.size() >= data->config.queueCapacity) { ++data->stats.dropped; return AddResult::Dropped; }
    Experience<O> e;
    for (unsigned i = 0; i < O; ++i) { e.observation[i] = observation[i]; e.nextObservation[i] = nextObservation[i]; }
    for (unsigned i = 0; i < ActionDim; ++i) e.action[i] = action[i];
    e.reward = reward;
    e.terminated = terminated;
    e.truncated = truncated;
    data->queue.push_back(e);
    ++data->stats.accepted;
    data->stats.queued = static_cast<unsigned>(data->queue.size());
    data->wake.notify_one();
    return AddResult::Accepted;
}

template <unsigned O>
bool BasicOnlineSACLearner<O>::Act(const float* observation, unsigned observationSize, float* action, unsigned actionSize, bool deterministic, unsigned* version) const {
    if (!observation || !action || observationSize != O || actionSize != ActionDim || !Finite(observation, O)) return false;
    const std::shared_ptr<const Policy> current = data->Current();
    deterministic = deterministic || data->frozen;
    Device device;
    Rng rng;
    rlt::malloc(device, rng);
    rlt::init(device, rng, static_cast<TI>(data->config.seed * 7919u + data->actCalls.fetch_add(1)));
    if (!deterministic && current->version == 0) {
        for (unsigned i = 0; i < ActionDim; ++i) action[i] = rlt::random::uniform_real_distribution(device.random, (T)-1, (T)1, rng);
    } else {
        current->Evaluate(observation, action, rng, !deterministic);
    }
    rlt::free(device, rng);
    if (version) *version = current->version;
    return true;
}

template <unsigned O>
std::shared_ptr<const typename BasicOnlineSACLearner<O>::Policy> BasicOnlineSACLearner<O>::CurrentPolicy() const { return data->Current(); }
template <unsigned O>
bool BasicOnlineSACLearner<O>::ActWith(const Policy& policy, const float* observation, unsigned observationSize, float* action, unsigned actionSize) {
    if (!observation || !action || observationSize != O || actionSize != ActionDim || !Finite(observation, O)) return false;
    Device device;
    Rng rng;
    rlt::malloc(device, rng);
    rlt::init(device, rng, 0);
    policy.Evaluate(observation, action, rng, false);
    rlt::free(device, rng);
    return true;
}
template <unsigned O>
unsigned BasicOnlineSACLearner<O>::VersionOf(const Policy& policy) { return policy.version; }

template <unsigned O>
void BasicOnlineSACLearner<O>::SetFrozen(bool frozen) {
    std::lock_guard<std::mutex> lock(data->mutex);
    data->frozen = frozen;
    data->stats.frozen = frozen;
}
template <unsigned O>
unsigned BasicOnlineSACLearner<O>::Version() const { return data->Current()->version; }
template <unsigned O>
OnlineSACStats BasicOnlineSACLearner<O>::ReadStats() const {
    std::lock_guard<std::mutex> lock(data->mutex);
    return data->stats;
}
template <unsigned O>
bool BasicOnlineSACLearner<O>::WaitForLearner(unsigned maxBacklog, unsigned timeoutMs) {
    std::unique_lock<std::mutex> lock(data->mutex);
    return data->roundDone.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
        return data->stop || data->stats.accepted - data->covered <= maxBacklog;
    });
}
template <unsigned O>
void BasicOnlineSACLearner<O>::Shutdown() { data->StopAndJoin(); }

template class BasicOnlineSACLearner<ObservationDim>;
template class BasicOnlineSACLearner<ChaseObservationSize>;
}
