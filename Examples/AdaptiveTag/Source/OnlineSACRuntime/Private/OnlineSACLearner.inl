// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "OnlineSACLearner.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

namespace online_sac {
using namespace detail;
namespace {
using LearnerClock = std::chrono::steady_clock;
using InferenceActor = ChaseSACConfig::EVAL_ACTOR_TYPE::template CHANGE_CAPABILITY<rlt::nn::capability::Forward<true>>;

struct Experience {
    float observation[ObservationDim], action[ActionDim], nextObservation[ObservationDim];
    float reward;
    bool terminated, truncated;
};
struct Snapshot {
    Device device;
    InferenceActor actor;
    unsigned version;
    template <typename SOURCE>
    Snapshot(Device& sourceDevice, SOURCE& source, unsigned number) : version(number) {
        rlt::malloc(device, actor);
        rlt::copy(sourceDevice, device, source, actor);
    }
    ~Snapshot() { rlt::free(device, actor); }
};
bool Finite(const float* values, unsigned count) {
    for (unsigned i = 0; i < count; ++i) if (!std::isfinite(values[i])) return false;
    return true;
}

constexpr char CheckpointMagic[8] = {'O', 'S', 'A', 'C', 'C', 'K', 'P', 'T'};
constexpr uint32_t CheckpointFormat = 1;
struct CheckpointHeader {
    char magic[8];
    uint32_t format, observationDim, actionDim, version;
    uint64_t updates, payloadSize;
    uint32_t checksum, reserved;
};
static_assert(sizeof(CheckpointHeader) == 48);
uint32_t Fnv1a(const char* bytes, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) { hash ^= static_cast<unsigned char>(bytes[i]); hash *= 16777619u; }
    return hash;
}
struct TarEntry { std::string name; size_t offset, size; };
// Independent, non-aborting walk over a ustar archive. Returns false on any malformed header or out-of-bounds entry.
bool ListTar(const char* data, size_t size, std::vector<TarEntry>& entries) {
    constexpr size_t Block = 512;
    size_t at = 0;
    for (;;) {
        if (at + Block > size) return false;
        const char* h = data + at;
        if (h[0] == '\0') return true;
        if (std::memcmp(h + 257, "ustar", 5) != 0) return false;
        size_t length = 0;
        for (int i = 0; i < 12 && h[124 + i] >= '0' && h[124 + i] <= '7'; ++i) length = length * 8 + (h[124 + i] - '0');
        at += Block;
        if (length > size - at) return false;
        entries.push_back({std::string(h, strnlen(h, 100)), at, length});
        at += (length + Block - 1) / Block * Block;
    }
}
bool EndsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}
bool SameStructure(const std::vector<char>& reference, const char* data, size_t size) {
    std::vector<TarEntry> a, b;
    if (!ListTar(reference.data(), reference.size(), a) || !ListTar(data, size, b) || a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name || a[i].size != b[i].size) return false;
        if (EndsWith(a[i].name, "meta") && std::memcmp(reference.data() + a[i].offset, data + b[i].offset, a[i].size) != 0) return false;
    }
    return true;
}
}

struct OnlineSACLearner::Impl {
    OnlineSACConfig config;
    Device device;
    std::unique_ptr<ChaseSACState> ts;
    std::thread thread;

    mutable std::mutex mutex;
    std::condition_variable wake, roundDone;
    std::deque<Experience> queue;
    OnlineSACStats stats;
    uint64_t covered = 0;
    bool stop = false;
    std::atomic<bool> stopFlag{false}, frozen{false};

    // Held by Run() while storing, updating and publishing, and by checkpoint save/load. Guards *ts and version.
    std::mutex stateMutex;
    unsigned version = 0;

    mutable std::mutex snapshotMutex;
    std::shared_ptr<const Snapshot> snapshot;
    mutable std::atomic<uint64_t> actCalls{0};

    explicit Impl(const OnlineSACConfig& value) : config(value) {
        if (config.updateEvery == 0) config.updateEvery = 1;
        if (config.queueCapacity == 0) config.queueCapacity = 1;
        frozen = config.frozen;
        stats.frozen = config.frozen;
        ts = std::make_unique<ChaseSACState>();
        rlt::malloc(device, *ts);
        rlt::init(device, *ts, config.seed);
        snapshot = std::make_shared<const Snapshot>(device, ts->actor_critic.actor, 0u);
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
    void Store(const Experience& e) {
        auto& buffer = rlt::get(ts->off_policy_runner.replay_buffers, 0, 0);
        rlt::Matrix<rlt::matrix::Specification<T, TI, 1, ObservationDim, false>> observation, next;
        rlt::Matrix<rlt::matrix::Specification<T, TI, 1, ActionDim, false>> action;
        for (TI i = 0; i < ObservationDim; ++i) { rlt::set(observation, 0, i, e.observation[i]); rlt::set(next, 0, i, e.nextObservation[i]); }
        for (TI i = 0; i < ActionDim; ++i) rlt::set(action, 0, i, e.action[i]);
        const ChaseEnv::State unusedState{};
        rlt::add(device, buffer, unusedState, observation, observation, action, e.reward, unusedState, next, next, e.terminated, e.truncated || e.terminated);
    }
    void Update() {
        ChaseSACState& s = *ts;
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
        std::vector<Experience> batch;
        uint64_t pending = 0, storedTotal = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stop || !queue.empty(); });
                if (stop) return;
                batch.assign(queue.begin(), queue.end());
                queue.clear();
                stats.queued = 0;
            }
            {
                std::lock_guard<std::mutex> state(stateMutex);
                for (const Experience& e : batch) Store(e);
            }
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
            std::unique_lock<std::mutex> state(stateMutex);
            for (; done < updates && !stopFlag; ++done) Update();
            if (stopFlag) return;
            if (updates > 0) Publish(version + 1);
            {
                std::lock_guard<std::mutex> lock(mutex);
                stats.updates += done;
                stats.version = version;
                stats.rounds += 1;
                stats.training = false;
                stats.lastRoundMs = std::chrono::duration<double, std::milli>(LearnerClock::now() - start).count();
                covered += pending;
            }
            state.unlock();
            pending = 0;
            roundDone.notify_all();
        }
    }
    // Caller holds stateMutex.
    void Publish(unsigned number) {
        version = number;
        auto next = std::make_shared<const Snapshot>(device, ts->actor_critic.actor, number);
        std::lock_guard<std::mutex> lock(snapshotMutex);
        snapshot = std::move(next);
    }
    // Caller holds stateMutex.
    std::vector<char> Serialize() {
        namespace tar = rlt::persist::backends::tar;
        tar::Writer writer;
        tar::WriterGroup<tar::WriterGroupSpecification<TI, tar::Writer>> root;
        root.writer = &writer;
        auto& ac = ts->actor_critic;
        { auto g = rlt::create_group(device, root, "actor"); rlt::save(device, ac.actor, g); }
        { auto g = rlt::create_group(device, root, "critic0"); rlt::save(device, ac.critics[0], g); }
        { auto g = rlt::create_group(device, root, "critic1"); rlt::save(device, ac.critics[1], g); }
        { auto g = rlt::create_group(device, root, "critic_target0"); rlt::save(device, ac.critics_target[0], g); }
        { auto g = rlt::create_group(device, root, "critic_target1"); rlt::save(device, ac.critics_target[1], g); }
        { auto g = rlt::create_group(device, root, "actor_optimizer"); rlt::save(device, ac.actor_optimizer, g); }
        { auto g = rlt::create_group(device, root, "critic_optimizer0"); rlt::save(device, ac.critic_optimizers[0], g); }
        { auto g = rlt::create_group(device, root, "critic_optimizer1"); rlt::save(device, ac.critic_optimizers[1], g); }
        { auto g = rlt::create_group(device, root, "alpha_optimizer"); rlt::save(device, ac.alpha_optimizer, g); }
        { auto g = rlt::create_group(device, root, "training"); rlt::save(device, ts->rng, g); }
        tar::finalize(device, writer);
        return std::move(writer.buffer);
    }
    // Caller holds stateMutex and has verified the structure against Serialize(), so no RLtools assert can fire.
    bool Deserialize(const char* data, size_t size) {
        namespace tar = rlt::persist::backends::tar;
        tar::ReaderGroup<tar::ReaderGroupSpecification<TI>> root;
        root.data.data = data;
        root.data.size = static_cast<TI>(size);
        auto& ac = ts->actor_critic;
        bool ok = true;
        { auto g = rlt::get_group(device, root, "actor"); ok = ok && rlt::load(device, ac.actor, g); }
        { auto g = rlt::get_group(device, root, "critic0"); ok = ok && rlt::load(device, ac.critics[0], g); }
        { auto g = rlt::get_group(device, root, "critic1"); ok = ok && rlt::load(device, ac.critics[1], g); }
        { auto g = rlt::get_group(device, root, "critic_target0"); ok = ok && rlt::load(device, ac.critics_target[0], g); }
        { auto g = rlt::get_group(device, root, "critic_target1"); ok = ok && rlt::load(device, ac.critics_target[1], g); }
        { auto g = rlt::get_group(device, root, "actor_optimizer"); ok = ok && rlt::load(device, ac.actor_optimizer, g); }
        { auto g = rlt::get_group(device, root, "critic_optimizer0"); ok = ok && rlt::load(device, ac.critic_optimizers[0], g); }
        { auto g = rlt::get_group(device, root, "critic_optimizer1"); ok = ok && rlt::load(device, ac.critic_optimizers[1], g); }
        { auto g = rlt::get_group(device, root, "alpha_optimizer"); ok = ok && rlt::load(device, ac.alpha_optimizer, g); }
        { auto g = rlt::get_group(device, root, "training"); ok = ok && rlt::load(device, ts->rng, g); }
        return ok;
    }
    std::shared_ptr<const Snapshot> Current() const {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        return snapshot;
    }
};

OnlineSACLearner::OnlineSACLearner(const OnlineSACConfig& config) : data(std::make_unique<Impl>(config)) {}
OnlineSACLearner::~OnlineSACLearner() = default;

AddResult OnlineSACLearner::AddExperience(const float* observation, unsigned observationSize, const float* action, unsigned actionSize, float reward,
    const float* nextObservation, unsigned nextObservationSize, bool terminated, bool truncated) {
    const bool valid = observation && action && nextObservation
        && observationSize == ObservationDim && actionSize == ActionDim && nextObservationSize == ObservationDim
        && Finite(observation, ObservationDim) && Finite(nextObservation, ObservationDim) && Finite(action, ActionDim) && std::isfinite(reward)
        && std::fabs(action[0]) <= 1.0f && std::fabs(action[1]) <= 1.0f;
    std::lock_guard<std::mutex> lock(data->mutex);
    if (!valid) { ++data->stats.rejected; return AddResult::Rejected; }
    if (data->frozen) { ++data->stats.ignoredFrozen; return AddResult::Frozen; }
    if (data->stop) { ++data->stats.dropped; return AddResult::Dropped; }
    if (data->queue.size() >= data->config.queueCapacity) { ++data->stats.dropped; return AddResult::Dropped; }
    Experience e;
    for (unsigned i = 0; i < ObservationDim; ++i) { e.observation[i] = observation[i]; e.nextObservation[i] = nextObservation[i]; }
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

bool OnlineSACLearner::Act(const float* observation, unsigned observationSize, float* action, unsigned actionSize, bool deterministic, unsigned* version) const {
    if (!observation || !action || observationSize != ObservationDim || actionSize != ActionDim || !Finite(observation, ObservationDim)) return false;
    const std::shared_ptr<const Snapshot> current = data->Current();
    deterministic = deterministic || data->frozen;
    Device device;
    Rng rng;
    rlt::malloc(device, rng);
    rlt::init(device, rng, static_cast<TI>(data->config.seed * 7919u + data->actCalls.fetch_add(1)));
    if (!deterministic && current->version == 0) {
        for (unsigned i = 0; i < ActionDim; ++i) action[i] = rlt::random::uniform_real_distribution(device.random, (T)-1, (T)1, rng);
    } else {
        RowTensor<ObservationDim> input;
        RowTensor<ActionDim> output;
        InferenceActor::Buffer<true> buffer;
        rlt::malloc(device, buffer);
        for (TI i = 0; i < ObservationDim; ++i) rlt::set(device, input, observation[i], 0, 0, i);
        if (deterministic) rlt::evaluate(device, current->actor, input, output, buffer, rng, rlt::Mode<rlt::mode::Evaluation<>>{});
        else rlt::evaluate(device, current->actor, input, output, buffer, rng, rlt::Mode<rlt::mode::Rollout<>>{});
        for (TI i = 0; i < ActionDim; ++i) action[i] = rlt::get(device, output, 0, 0, i);
        rlt::free(device, buffer);
    }
    rlt::free(device, rng);
    if (version) *version = current->version;
    return true;
}

void OnlineSACLearner::SetFrozen(bool frozen) {
    std::lock_guard<std::mutex> lock(data->mutex);
    data->frozen = frozen;
    data->stats.frozen = frozen;
}
unsigned OnlineSACLearner::Version() const { return data->Current()->version; }
OnlineSACStats OnlineSACLearner::ReadStats() const {
    std::lock_guard<std::mutex> lock(data->mutex);
    return data->stats;
}
bool OnlineSACLearner::WaitForLearner(unsigned maxBacklog, unsigned timeoutMs) {
    std::unique_lock<std::mutex> lock(data->mutex);
    return data->roundDone.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
        return data->stop || data->stats.accepted - data->covered <= maxBacklog;
    });
}
CheckpointResult OnlineSACLearner::SaveCheckpoint(const char* path) {
    if (!path) return CheckpointResult::IoError;
    std::vector<char> payload;
    CheckpointHeader header{};
    {
        std::lock_guard<std::mutex> state(data->stateMutex);
        payload = data->Serialize();
        header.version = data->version;
        std::lock_guard<std::mutex> lock(data->mutex);
        header.updates = data->stats.updates;
    }
    std::memcpy(header.magic, CheckpointMagic, sizeof(header.magic));
    header.format = CheckpointFormat;
    header.observationDim = ObservationDim;
    header.actionDim = ActionDim;
    header.payloadSize = payload.size();
    header.checksum = Fnv1a(payload.data(), payload.size());
    const std::string tmp = std::string(path) + ".tmp";
    FILE* file = std::fopen(tmp.c_str(), "wb");
    if (!file) return CheckpointResult::IoError;
    bool ok = std::fwrite(&header, sizeof(header), 1, file) == 1 && std::fwrite(payload.data(), 1, payload.size(), file) == payload.size();
    ok = (std::fclose(file) == 0) && ok;
    if (!ok || std::rename(tmp.c_str(), path) != 0) { std::remove(tmp.c_str()); return CheckpointResult::IoError; }
    return CheckpointResult::Ok;
}
CheckpointResult OnlineSACLearner::LoadCheckpoint(const char* path) {
    if (!path) return CheckpointResult::IoError;
    FILE* file = std::fopen(path, "rb");
    if (!file) return CheckpointResult::IoError;
    std::vector<char> bytes;
    char chunk[65536];
    for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), file)) > 0;) bytes.insert(bytes.end(), chunk, chunk + n);
    const bool readError = std::ferror(file) != 0;
    std::fclose(file);
    if (readError) return CheckpointResult::IoError;
    CheckpointHeader header;
    if (bytes.size() < sizeof(header)) return CheckpointResult::BadFormat;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (std::memcmp(header.magic, CheckpointMagic, sizeof(header.magic)) != 0 || header.format != CheckpointFormat) return CheckpointResult::BadFormat;
    if (header.payloadSize != bytes.size() - sizeof(header)) return CheckpointResult::BadFormat;
    const char* payload = bytes.data() + sizeof(header);
    if (Fnv1a(payload, header.payloadSize) != header.checksum) return CheckpointResult::BadFormat;
    if (header.observationDim != ObservationDim || header.actionDim != ActionDim) return CheckpointResult::Mismatch;
    std::lock_guard<std::mutex> state(data->stateMutex);
    if (!SameStructure(data->Serialize(), payload, header.payloadSize)) return CheckpointResult::Mismatch;
    if (!data->Deserialize(payload, header.payloadSize)) return CheckpointResult::BadFormat; // unreachable after the structural match
    const unsigned next = std::max(data->version + 1, header.version);
    data->Publish(next);
    std::lock_guard<std::mutex> lock(data->mutex);
    data->stats.version = next;
    return CheckpointResult::Ok;
}
void OnlineSACLearner::Shutdown() { data->StopAndJoin(); }
}
