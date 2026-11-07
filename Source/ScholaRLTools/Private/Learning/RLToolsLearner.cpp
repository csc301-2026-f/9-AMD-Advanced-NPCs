// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// All RLtools operation definitions are compiled together, outside unity/PCH builds.
#ifdef UE_MODULE_NAME
#include "HAL/Platform.h"
#endif
#include "RLToolsLearner.h"
#include "Generated/RLToolsLearnerParameters.h"
#ifdef SCHOLA_RLTOOLS_PROJECT_PARAMETERS
#include SCHOLA_RLTOOLS_PROJECT_PARAMETERS
#endif
#include <unordered_map>
#include <rl_tools/operations/cpu_mux.h>
#include <rl_tools/nn/optimizers/adam/instance/operations_generic.h>
#include <rl_tools/nn/operations_cpu_mux.h>
#include <rl_tools/nn/layers/sample_and_squash/operations_generic.h>
#include <rl_tools/nn/layers/td3_sampling/operations_generic.h>
#include <rl_tools/nn/layers/standardize/operations_generic.h>
#include <rl_tools/nn_models/mlp/operations_generic.h>
#include <rl_tools/nn_models/mlp_unconditional_stddev/operations_generic.h>
#include <rl_tools/nn_models/sequential/operations_generic.h>
#include <rl_tools/nn/optimizers/adam/operations_generic.h>
#include "RLToolsSpaces.h"
#include <rl_tools/rl/algorithms/sac/loop/core/config.h>
#include <rl_tools/rl/algorithms/sac/loop/core/operations_generic.h>
#include <rl_tools/rl/algorithms/td3/loop/core/config.h>
#include <rl_tools/rl/algorithms/td3/loop/core/operations_generic.h>
#include <rl_tools/rl/algorithms/ppo/loop/core/config.h>
#include <rl_tools/rl/algorithms/ppo/loop/core/operations_generic.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <random>

namespace Schola::RLTools
{
namespace rlt = rl_tools;
using FDevice = rlt::devices::DEVICE_FACTORY<>;
using TI = FDevice::index_t;
using FTypePolicy = rlt::numeric_types::Policy<float>;
using FRng = FDevice::SPEC::RANDOM::ENGINE<>;
template<EActivation Activation>
constexpr auto NativeActivation = Activation == EActivation::Tanh
    ? rlt::nn::activation_functions::TANH : rlt::nn::activation_functions::RELU;

static bool Finite(const std::vector<float>& Values, unsigned Count)
{
    return Values.size() == Count && std::all_of(Values.begin(), Values.end(),
        [](float Value) { return std::isfinite(Value); });
}

struct FDecisionContext
{
    std::shared_ptr<const FPolicy> Policy;
    std::vector<float> Observation, Action, RawAction;
    float LogProbability = 0, Value = 0;
    std::uint64_t Agent = 0, Episode = 0, Sequence = 0;
    std::atomic<bool> Submitted{false};
};

const std::vector<float>& FDecision::GetAction() const
{
    static const std::vector<float> Empty;
    return Context ? Context->Action : Empty;
}
std::uint64_t FDecision::GetVersion() const { return Context ? Context->Policy->GetVersion() : 0; }
std::uint64_t FDecision::GetGeneration() const { return Context ? Context->Policy->GetGeneration() : 0; }

bool FPolicy::Preprocess(const std::vector<float>& Observation, std::vector<float>& Result) const
{
    if (!Finite(Observation, Profile.Observations)) return false;
    Result = Observation;
    if (!Settings.ObservationMean.empty())
    {
        for (unsigned Index = 0; Index < Profile.Observations; ++Index)
            Result[Index] = (Result[Index] - Settings.ObservationMean[Index]) / Settings.ObservationStd[Index];
    }
    return Finite(Result, Profile.Observations);
}
bool FPolicy::Evaluate(const std::vector<float>& Observation, std::vector<float>& Action) const
{
    std::vector<float> Input, Result;
    float LogProbability = 0, Value = 0;
    if (!Preprocess(Observation, Input) || !Sample(Input, true, 0, Result, LogProbability, Value)) return false;
    for (float& Entry : Result) Entry = std::clamp(Entry, -1.0f, 1.0f);
    if (!Finite(Result, Profile.Actions)) return false;
    Action = std::move(Result);
    return true;
}
bool FPolicy::EvaluatePrepared(const std::vector<float>& Observation, float& Value) const
{
    if (!Finite(Observation, Profile.Observations)) return false;
    std::vector<float> Action;
    float LogProbability = 0;
    return Sample(Observation, true, 0, Action, LogProbability, Value);
}

struct FExperience
{
    std::shared_ptr<FDecisionContext> Decision;
    std::vector<float> Observation, Action, NextObservation;
    float Reward;
    bool Terminated, Truncated;
};

struct FAlgorithmState
{
    virtual ~FAlgorithmState() = default;
    virtual void Store(const FExperience& Experience) = 0;
    virtual bool Ready() const = 0;
    virtual void Update(FStats& Stats) = 0;
    virtual unsigned Occupancy() const = 0;
    virtual bool IterationComplete() const { return true; }
    virtual std::shared_ptr<FPolicy> Snapshot() = 0;
};

template <unsigned O, unsigned A, unsigned H, unsigned B, unsigned R, typename TParameters>
struct TSACParameters : rlt::rl::algorithms::sac::loop::core::DefaultParameters<FTypePolicy, TI,
    rlt::rl::environments::ScholaSpaces<float, TI, O, A>>
{
    static constexpr TI REPLAY_BUFFER_CAP = R, ACTOR_HIDDEN_DIM = H;
    static constexpr TI CRITIC_HIDDEN_DIM = TParameters::CriticHiddenWidth ? TParameters::CriticHiddenWidth : H;
    static constexpr TI ACTOR_NUM_LAYERS = TParameters::ActorLayers, CRITIC_NUM_LAYERS = TParameters::CriticLayers;
    static constexpr auto ACTOR_ACTIVATION_FUNCTION = NativeActivation<TParameters::ActorActivation>;
    static constexpr auto CRITIC_ACTIVATION_FUNCTION = NativeActivation<TParameters::CriticActivation>;
    static_assert(O > 0 && A > 0 && H > 0 && B > 1 && R >= B);
    static_assert(TParameters::Discount > 0 && TParameters::Discount <= 1);
    static_assert(TParameters::TargetTau > 0 && TParameters::TargetTau <= 1);
    static constexpr TI N_WARMUP_STEPS = B;
    static constexpr bool SAMPLE_ENVIRONMENT_PARAMETERS = false;
    struct SAC_PARAMETERS : rlt::rl::algorithms::sac::DefaultParameters<FTypePolicy, TI, A>
    {
        static constexpr TI ACTOR_BATCH_SIZE = B, CRITIC_BATCH_SIZE = B;
        static constexpr float GAMMA = TParameters::Discount;
        static constexpr float CRITIC_POLYAK = 1.0f - TParameters::TargetTau;
        static constexpr float ACTOR_POLYAK = 1.0f - TParameters::TargetTau;
        static constexpr TI ACTOR_TRAINING_INTERVAL = TParameters::ActorInterval ? TParameters::ActorInterval : 1;
        static constexpr TI CRITIC_TARGET_UPDATE_INTERVAL = TParameters::TargetInterval ? TParameters::TargetInterval : 1;
        static constexpr float ALPHA = TParameters::EntropyCoefficient;
        static constexpr bool ADAPTIVE_ALPHA = TParameters::AdaptiveEntropy;
    };
};
template <unsigned O, unsigned A, unsigned H, unsigned B, unsigned R, typename TParameters>
struct TTD3Parameters : rlt::rl::algorithms::td3::loop::core::DefaultParameters<FTypePolicy, TI,
    rlt::rl::environments::ScholaSpaces<float, TI, O, A>>
{
    static constexpr TI REPLAY_BUFFER_CAP = R, ACTOR_HIDDEN_DIM = H;
    static constexpr TI CRITIC_HIDDEN_DIM = TParameters::CriticHiddenWidth ? TParameters::CriticHiddenWidth : H;
    static constexpr TI ACTOR_NUM_LAYERS = TParameters::ActorLayers, CRITIC_NUM_LAYERS = TParameters::CriticLayers;
    static constexpr auto ACTOR_ACTIVATION_FUNCTION = NativeActivation<TParameters::ActorActivation>;
    static constexpr auto CRITIC_ACTIVATION_FUNCTION = NativeActivation<TParameters::CriticActivation>;
    struct TD3_PARAMETERS : rlt::rl::algorithms::td3::DefaultParameters<FTypePolicy, TI>
    {
        static constexpr TI ACTOR_BATCH_SIZE = B, CRITIC_BATCH_SIZE = B;
        static constexpr float GAMMA = TParameters::Discount;
        static constexpr float CRITIC_POLYAK = 1.0f - TParameters::TargetTau;
        static constexpr float ACTOR_POLYAK = 1.0f - TParameters::TargetTau;
        static constexpr TI ACTOR_TRAINING_INTERVAL = TParameters::ActorInterval ? TParameters::ActorInterval : 2;
        static constexpr TI CRITIC_TARGET_UPDATE_INTERVAL = TParameters::TargetInterval ? TParameters::TargetInterval : 2;
        static constexpr TI ACTOR_TARGET_UPDATE_INTERVAL = CRITIC_TARGET_UPDATE_INTERVAL;
        static constexpr float TARGET_NEXT_ACTION_NOISE_STD = TParameters::TargetActionNoise;
        static constexpr float TARGET_NEXT_ACTION_NOISE_CLIP = TParameters::TargetActionNoiseClip;
    };
};
template <unsigned O, unsigned A, unsigned H, unsigned B, unsigned L, typename TParameters>
struct TPPOParameters : rlt::rl::algorithms::ppo::loop::core::DefaultParameters<FTypePolicy, TI,
    rlt::rl::environments::ScholaSpaces<float, TI, O, A>>
{
    static_assert(O > 0 && A > 0 && H > 0 && B > 1 && L >= B && L % B == 0);
    static_assert(TParameters::Discount > 0 && TParameters::Discount <= 1);
    static_assert(TParameters::GAE >= 0 && TParameters::GAE <= 1 && TParameters::PPOClip > 0);
    static constexpr TI N_ENVIRONMENTS = 1, ON_POLICY_RUNNER_STEPS_PER_ENV = L;
    static constexpr TI DATASET_SIZE = L, BATCH_SIZE = B;
    static constexpr TI ACTOR_HIDDEN_DIM = H;
    static constexpr TI CRITIC_HIDDEN_DIM = TParameters::CriticHiddenWidth ? TParameters::CriticHiddenWidth : H;
    static constexpr TI ACTOR_NUM_LAYERS = TParameters::ActorLayers, CRITIC_NUM_LAYERS = TParameters::CriticLayers;
    static constexpr auto ACTOR_ACTIVATION_FUNCTION = NativeActivation<TParameters::ActorActivation>;
    static constexpr auto CRITIC_ACTIVATION_FUNCTION = NativeActivation<TParameters::CriticActivation>;
    struct PPO_PARAMETERS : rlt::rl::algorithms::ppo::DefaultParameters<FTypePolicy, TI, B>
    {
        static constexpr TI N_EPOCHS = 1;
        static constexpr float GAMMA = TParameters::Discount;
        static constexpr float LAMBDA = TParameters::GAE;
        static constexpr float EPSILON_CLIP = TParameters::PPOClip;
        static constexpr float ACTION_ENTROPY_COEFFICIENT = TParameters::PPOEntropyCoefficient;
        static constexpr bool NORMALIZE_ADVANTAGE = TParameters::NormalizeAdvantage;
        static constexpr float INITIAL_ACTION_STD = TParameters::InitialActionStd;
        static constexpr bool LEARN_ACTION_STD = TParameters::LearnActionStd;
    };
};

template <typename TOptimizer>
static void SetLearningRate(FDevice& Device, TOptimizer& Optimizer, float Rate)
{
    auto Parameters = rlt::get(Device, Optimizer.parameters, 0);
    Parameters.alpha = Rate;
    rlt::set(Device, Optimizer.parameters, Parameters, 0);
}

/** Each evaluation owns scratch/RNG; immutable weights can be read by several agents. */
template <typename TActor, unsigned O, unsigned A>
struct TPolicy : FPolicy
{
    FDevice Device;
    TActor Actor;
    TPolicy(FDevice& SourceDevice, const TActor& Source)
    {
        rlt::malloc(Device, Actor);
        rlt::copy(SourceDevice, Device, Source, Actor);
    }
    template <typename TSource>
    TPolicy(FDevice& SourceDevice, const TSource& Source)
    {
        rlt::malloc(Device, Actor);
        rlt::copy(SourceDevice, Device, Source, Actor);
    }
    ~TPolicy() override { rlt::free(Device, Actor); }
    bool Sample(const std::vector<float>& Observation, bool Deterministic, unsigned Seed,
        std::vector<float>& Action, float& LogProbability, float& Value) const override
    {
        FDevice Local;
        FRng Rng;
        rlt::init(Local, Rng, Seed);
        typename TActor::template Buffer<true> Buffer;
        rlt::malloc(Local, Buffer);
        rlt::Tensor<rlt::tensor::Specification<float, TI, rlt::tensor::Shape<TI, 1, 1, O>, false>> Input;
        rlt::Tensor<rlt::tensor::Specification<float, TI, rlt::tensor::Shape<TI, 1, 1, A>, false>> Output;
        for (TI Index = 0; Index < O; ++Index) rlt::set(Local, Input, Observation[Index], 0, 0, Index);
        // TD3 exploration is applied once, using the runtime setting, after its actor mean.
        if (Deterministic || Profile.Algorithm == EAlgorithm::TD3)
            rlt::evaluate(Local, Actor, Input, Output, Buffer, Rng, rlt::Mode<rlt::mode::Evaluation<>>{});
        else rlt::evaluate(Local, Actor, Input, Output, Buffer, Rng, rlt::Mode<rlt::mode::Rollout<>>{});
        Action.resize(A);
        for (TI Index = 0; Index < A; ++Index) Action[Index] = rlt::get(Local, Output, 0, 0, Index);
        rlt::free(Local, Buffer);
        LogProbability = Value = 0;
        return Finite(Action, A);
    }
};

template <typename TActor, typename TCritic, unsigned O, unsigned A>
struct TPPOPolicy : TPolicy<TActor, O, A>
{
    TCritic Critic;
    template <typename TActorSource, typename TCriticSource>
    TPPOPolicy(FDevice& Device, const TActorSource& Actor, const TCriticSource& Source)
        : TPolicy<TActor, O, A>(Device, Actor)
    {
        rlt::malloc(this->Device, Critic);
        rlt::copy(Device, this->Device, Source, Critic);
    }
    ~TPPOPolicy() override { rlt::free(this->Device, Critic); }
    bool Sample(const std::vector<float>& Observation, bool Deterministic, unsigned Seed,
        std::vector<float>& Action, float& LogProbability, float& Value) const override
    {
        if (!TPolicy<TActor, O, A>::Sample(Observation, true, Seed, Action, LogProbability, Value)) return false;
        FDevice Local;
        FRng Rng;
        rlt::init(Local, Rng, Seed);
        LogProbability = 0;
        const auto& LastLayer = rlt::get_last_layer(this->Actor);
        for (TI Index = 0; Index < A; ++Index)
        {
            const float Mean = Action[Index];
            const float LogStd = rlt::get(Local, LastLayer.log_std.parameters, Index);
            if (!Deterministic) Action[Index] = rlt::random::normal_distribution::sample(Local.random, Mean, std::exp(LogStd), Rng);
            LogProbability += rlt::random::normal_distribution::log_prob(Local.random, Mean, LogStd, Action[Index]);
        }
        typename TCritic::template Buffer<true> Buffer;
        rlt::malloc(Local, Buffer);
        rlt::Tensor<rlt::tensor::Specification<float, TI, rlt::tensor::Shape<TI, 1, 1, O>, false>> Input;
        rlt::Tensor<rlt::tensor::Specification<float, TI, rlt::tensor::Shape<TI, 1, 1, 1>, false>> Output;
        for (TI Index = 0; Index < O; ++Index) rlt::set(Local, Input, Observation[Index], 0, 0, Index);
        rlt::evaluate(Local, Critic, Input, Output, Buffer, Rng);
        Value = rlt::get(Local, Output, 0, 0, 0);
        rlt::free(Local, Buffer);
        return Finite(Action, A) && std::isfinite(Value) && std::isfinite(LogProbability);
    }
};

template <EAlgorithm Algorithm, unsigned O, unsigned A, unsigned H, unsigned B, unsigned R, typename TParameters>
struct TOffPolicyState : FAlgorithmState
{
    using Environment = rlt::rl::environments::ScholaSpaces<float, TI, O, A>;
    using SACConfig = rlt::rl::algorithms::sac::loop::core::Config<FTypePolicy, TI, FRng, Environment, TSACParameters<O,A,H,B,R,TParameters>>;
    using TD3Config = rlt::rl::algorithms::td3::loop::core::Config<FTypePolicy, TI, FRng, Environment, TTD3Parameters<O,A,H,B,R,TParameters>>;
    using Config = std::conditional_t<Algorithm == EAlgorithm::SAC, SACConfig, TD3Config>;
    using StateType = typename Config::template State<Config>;
    using Actor = typename Config::EVAL_ACTOR_TYPE::template CHANGE_CAPABILITY<rlt::nn::capability::Forward<true>>;
    FDevice Device;
    std::unique_ptr<StateType> State = std::make_unique<StateType>();
    unsigned Count = 0;
    explicit TOffPolicyState(const FSettings& Settings)
    {
        rlt::malloc(Device, *State);
        rlt::init(Device, *State, Settings.Seed);
        SetLearningRate(Device, State->actor_critic.actor_optimizer, Settings.LearningRate);
        for (auto& Optimizer : State->actor_critic.critic_optimizers) SetLearningRate(Device, Optimizer, Settings.LearningRate);
        if constexpr(Algorithm == EAlgorithm::SAC) SetLearningRate(Device, State->actor_critic.alpha_optimizer, Settings.LearningRate);
    }
    ~TOffPolicyState() override { rlt::free(Device, *State); }
    unsigned Occupancy() const override { return std::min(Count, R); }
    bool Ready() const override { return Count >= B; }
    void Store(const FExperience& Experience) override
    {
        auto& Replay = rlt::get(State->off_policy_runner.replay_buffers, 0, 0);
        rlt::Matrix<rlt::matrix::Specification<float, TI, 1, O, false>> Observation, Next;
        rlt::Matrix<rlt::matrix::Specification<float, TI, 1, A, false>> Action;
        for (TI Index = 0; Index < O; ++Index)
        {
            rlt::set(Observation, 0, Index, Experience.Observation[Index]);
            rlt::set(Next, 0, Index, Experience.NextObservation[Index]);
        }
        for (TI Index = 0; Index < A; ++Index) rlt::set(Action, 0, Index, Experience.Action[Index]);
        const typename Environment::State Unused{};
        // At the pinned revision, replay.truncated marks any episode boundary.
        rlt::add(Device, Replay, Unused, Observation, Observation, Action, Experience.Reward,
            Unused, Next, Next, Experience.Terminated, Experience.Truncated || Experience.Terminated);
        ++Count;
    }
    void Update(FStats& Stats) override
    {
        auto& S = *State;
        rlt::gather_batch(Device, S.off_policy_runner, S.critic_batch, S.rng);
        if constexpr(Algorithm == EAlgorithm::SAC)
        {
            rlt::randn(Device, S.action_noise_critic, S.rng);
            for (unsigned Index = 0; Index < 2; ++Index)
                rlt::train_critic(Device, S.actor_critic, S.actor_critic.critics[Index], S.critic_batch,
                    S.actor_critic.critic_optimizers[Index], S.actor_target_buffers[Index], S.critic_buffers[Index],
                    S.critic_target_buffers[Index], S.critic_training_buffers[Index], S.action_noise_critic, S.rng);
            if (Stats.Updates % Config::CORE_PARAMETERS::SAC_PARAMETERS::CRITIC_TARGET_UPDATE_INTERVAL == 0)
            {
                rlt::update_critic_targets(Device, S.actor_critic);
                ++Stats.TargetUpdates;
            }
            rlt::randn(Device, S.action_noise_actor, S.rng);
            // train_actor also performs adaptive entropy-temperature optimization in this revision.
            if (Stats.Updates % Config::CORE_PARAMETERS::SAC_PARAMETERS::ACTOR_TRAINING_INTERVAL == 0)
            {
                rlt::train_actor(Device, S.actor_critic, S.critic_batch, S.actor_critic.actor_optimizer,
                    S.actor_buffers[0], S.critic_buffers[0], S.actor_training_buffers, S.action_noise_actor, S.rng);
                ++Stats.ActorUpdates;
            }
        }
        else
        {
            auto Noise = rlt::matrix_view(Device, S.critic_training_buffers.target_next_action_noise);
            rlt::target_action_noise(Device, S.actor_critic, Noise, S.rng);
            for (unsigned Index = 0; Index < 2; ++Index)
                rlt::train_critic(Device, S.actor_critic, S.actor_critic.critics[Index], S.critic_batch,
                    S.actor_critic.critic_optimizers[Index], S.actor_buffers[Index], S.actor_target_buffers[Index],
                    S.critic_buffers[Index], S.critic_target_buffers[Index], S.critic_training_buffers, S.rng);
            // TD3 delays actor and target updates, as in the pinned standard loop.
            if (Stats.Updates % Config::CORE_PARAMETERS::TD3_PARAMETERS::ACTOR_TRAINING_INTERVAL == 0)
            {
                rlt::train_actor(Device, S.actor_critic, S.critic_batch, S.actor_critic.actor_optimizer,
                    S.actor_buffers[0], S.critic_buffers[0], S.actor_training_buffers, S.rng);
                ++Stats.ActorUpdates;
            }
            if (Stats.Updates % Config::CORE_PARAMETERS::TD3_PARAMETERS::CRITIC_TARGET_UPDATE_INTERVAL == 0)
            {
                rlt::update_critic_targets(Device, S.actor_critic);
                rlt::update_actor_target(Device, S.actor_critic);
                ++Stats.TargetUpdates;
            }
        }
        Stats.CriticUpdates += 2;
        ++Stats.Updates;
    }
    std::shared_ptr<FPolicy> Snapshot() override
    { return std::make_shared<TPolicy<Actor, O, A>>(Device, State->actor_critic.actor); }
};

template <unsigned O, unsigned A, unsigned H, unsigned B, unsigned L, typename TParameters>
struct TPPOState : FAlgorithmState
{
    using Environment = rlt::rl::environments::ScholaSpaces<float, TI, O, A>;
    using Config = rlt::rl::algorithms::ppo::loop::core::Config<FTypePolicy, TI, FRng, Environment, TPPOParameters<O,A,H,B,L,TParameters>>;
    using StateType = typename Config::template State<Config>;
    using Actor = typename Config::NN::ACTOR_TYPE::template CHANGE_BATCH_SIZE<TI, 1>::template CHANGE_CAPABILITY<rlt::nn::capability::Forward<true>>;
    using Critic = typename Config::NN::CRITIC_TYPE::template CHANGE_BATCH_SIZE<TI, 1>::template CHANGE_CAPABILITY<rlt::nn::capability::Forward<true>>;
    FDevice Device;
    std::unique_ptr<StateType> State = std::make_unique<StateType>();
    std::vector<FExperience> Rollout;
    unsigned Epoch = 0, Epochs;
    explicit TPPOState(const FSettings& Settings) : Epochs(Settings.PPOEpochs)
    {
        rlt::malloc(Device, *State);
        rlt::init(Device, *State, Settings.Seed);
        SetLearningRate(Device, State->actor_optimizer, Settings.LearningRate);
        SetLearningRate(Device, State->critic_optimizer, Settings.LearningRate);
        Rollout.reserve(L);
    }
    ~TPPOState() override { rlt::free(Device, *State); }
    unsigned Occupancy() const override { return static_cast<unsigned>(Rollout.size()); }
    bool Ready() const override { return Rollout.size() == L; }
    bool IterationComplete() const override { return Epoch == 0; }
    void Store(const FExperience& Experience) override { Rollout.push_back(Experience); }
    void Update(FStats& Stats) override
    {
        auto& Data = State->on_policy_runner_dataset;
        if (Epoch == 0)
        {
            // Group records into agent/episode trajectories; never propagate advantages across agents.
            std::stable_sort(Rollout.begin(), Rollout.end(), [](const FExperience& Left, const FExperience& Right)
            {
                const auto& Lhs = *Left.Decision;
                const auto& Rhs = *Right.Decision;
                if (Lhs.Agent != Rhs.Agent) return Lhs.Agent < Rhs.Agent;
                if (Lhs.Episode != Rhs.Episode) return Lhs.Episode < Rhs.Episode;
                return Lhs.Sequence < Rhs.Sequence;
            });
            float FollowingAdvantage = 0;
            for (int Index = L - 1; Index >= 0; --Index)
            {
                const auto& E = Rollout[Index];
                const auto& C = *E.Decision;
                float NextValue = 0;
                // Value and original action likelihood use the same immutable rollout policy.
                C.Policy->EvaluatePrepared(E.NextObservation, NextValue);
                const bool Continues = Index + 1 < static_cast<int>(L) && !E.Terminated && !E.Truncated
                    && C.Agent == Rollout[Index + 1].Decision->Agent && C.Episode == Rollout[Index + 1].Decision->Episode
                    && C.Sequence + 1 == Rollout[Index + 1].Decision->Sequence
                    && E.NextObservation == Rollout[Index + 1].Observation;
                const float Delta = E.Reward + (E.Terminated ? 0.0f : TParameters::Discount * NextValue) - C.Value;
                const float Advantage = Delta + (Continues ? TParameters::Discount * TParameters::GAE * FollowingAdvantage : 0.0f);
                FollowingAdvantage = Advantage;
                for (TI Dimension = 0; Dimension < O; ++Dimension) rlt::set(Data.observations, Index, Dimension, E.Observation[Dimension]);
                for (TI Dimension = 0; Dimension < A; ++Dimension) rlt::set(Data.actions, Index, Dimension, C.RawAction[Dimension]);
                rlt::set(Data.action_log_probs, Index, 0, C.LogProbability);
                rlt::set(Data.advantages, Index, 0, Advantage);
                rlt::set(Data.target_values, Index, 0, Advantage + C.Value);
                rlt::set(Data.reset, Index, 0, 0.0f);
            }
        }
        // One epoch per call provides a cooperative scheduling boundary between epochs.
        rlt::train(Device, State->ppo, Data, State->actor_optimizer, State->critic_optimizer,
            State->ppo_buffers, State->actor_buffers, State->critic_buffers, State->rng);
        Stats.ActorUpdates += L / B;
        Stats.CriticUpdates += L / B;
        ++Stats.Updates;
        if (++Epoch == Epochs) { Epoch = 0; Rollout.clear(); }
    }
    std::shared_ptr<FPolicy> Snapshot() override
    { return std::make_shared<TPPOPolicy<Actor, Critic, O, A>>(Device, State->ppo.actor, State->ppo.critic); }
};

// This registration include is the extension point for project-local generated configurations.
// Entries describe actual compiled implementations; unsupported requests never silently change shape.
template<EAlgorithm Algorithm, unsigned O, unsigned A, unsigned H, unsigned B, unsigned R, unsigned L, typename Parameters>
static std::unique_ptr<FAlgorithmState> CreateProfile(const FSettings& Settings)
{
    if constexpr (Algorithm == EAlgorithm::PPO) return std::make_unique<TPPOState<O,A,H,B,L,Parameters>>(Settings);
    else return std::make_unique<TOffPolicyState<Algorithm,O,A,H,B,R,Parameters>>(Settings);
}
#define SCHOLA_RLTOOLS_ALGORITHM_PROFILE(Kind, Identifier, O, A, H, B, R, L, Parameters) \
    if (Profile.Algorithm == EAlgorithm::Kind && Configuration == Identifier) \
        return CreateProfile<EAlgorithm::Kind,O,A,H,B,R,L,Parameters>(Settings);
#define SCHOLA_RLTOOLS_PROFILE(Identifier, O, A, H, B, R, L, Parameters) \
    SCHOLA_RLTOOLS_ALGORITHM_PROFILE(SAC, Identifier, O,A,H,B,R,L,Parameters) \
    SCHOLA_RLTOOLS_ALGORITHM_PROFILE(TD3, Identifier, O,A,H,B,R,L,Parameters) \
    SCHOLA_RLTOOLS_ALGORITHM_PROFILE(PPO, Identifier, O,A,H,B,R,L,Parameters)
static std::unique_ptr<FAlgorithmState> MakeState(const FProfile& Profile, const FSettings& Settings)
{
    const std::string Configuration = Profile.Id.substr(Profile.Id.find('/') + 1);
#include "RLToolsProfileRegistry.inl"
    return nullptr;
}
#undef SCHOLA_RLTOOLS_PROFILE
#undef SCHOLA_RLTOOLS_ALGORITHM_PROFILE

struct FLearner::FImpl
{
    FProfile Profile;
    FSettings Settings;
    std::unique_ptr<FAlgorithmState> State;
    mutable std::mutex AdmissionMutex, TrainingMutex;
    std::deque<FExperience> Queue;
    // Use the C++11 shared_ptr atomic operations instead of atomic<shared_ptr>.
    // Apple libc++ does not provide the C++20 specialization used by MSVC.
    std::shared_ptr<const FPolicy> Published;
    std::shared_ptr<const void> Owner = std::make_shared<int>(0);
    FStats Stats;
    std::unordered_map<std::uint64_t, std::uint64_t> AgentSequences;
    std::uint64_t RecordsSinceTrain = 0, UpdatesSincePublish = 0;
    bool PPOUpdating = false;
    std::atomic<bool> StopRequested{false};
    FImpl(const FProfile& InProfile, const FSettings& InSettings) : Profile(InProfile), Settings(InSettings)
    { State = MakeState(Profile, Settings); Publish(); }
    std::shared_ptr<const FPolicy> LoadPublished() const { return std::atomic_load(&Published); }
    void StorePublished(std::shared_ptr<const FPolicy> Policy) { std::atomic_store(&Published, std::move(Policy)); }
    std::shared_ptr<const FPolicy> ExchangePublished(std::shared_ptr<const FPolicy> Policy)
    { return std::atomic_exchange(&Published, std::move(Policy)); }
    void Publish()
    {
        auto Policy = State->Snapshot();
        Policy->Profile = Profile;
        Policy->Settings = Settings;
        Policy->Version = Stats.Version;
        Policy->Generation = Stats.Generation;
        Policy->Owner = Owner;
        StorePublished(std::move(Policy));
    }
};

template <typename TParameters>
static FProfile Describe(const std::string& Id, EAlgorithm Algorithm, unsigned O, unsigned A,
    unsigned H, unsigned B, unsigned R, unsigned L)
{
    FProfile P{Id, Algorithm, O, A, H, B, Algorithm == EAlgorithm::PPO ? 0u : R,
        Algorithm == EAlgorithm::PPO ? L : 0u};
    P.ActorLayers = TParameters::ActorLayers;
    P.CriticLayers = TParameters::CriticLayers;
    P.CriticHiddenWidth = TParameters::CriticHiddenWidth ? TParameters::CriticHiddenWidth : H;
    P.ActorActivation = TParameters::ActorActivation;
    P.CriticActivation = TParameters::CriticActivation;
    P.Discount = TParameters::Discount;
    P.TargetTau = TParameters::TargetTau;
    P.ActorInterval = TParameters::ActorInterval ? TParameters::ActorInterval : Algorithm == EAlgorithm::TD3 ? 2 : 1;
    P.TargetInterval = TParameters::TargetInterval ? TParameters::TargetInterval : Algorithm == EAlgorithm::TD3 ? 2 : 1;
    P.EntropyCoefficient = TParameters::EntropyCoefficient;
    P.AdaptiveEntropy = TParameters::AdaptiveEntropy;
    P.TargetActionNoise = TParameters::TargetActionNoise;
    P.TargetActionNoiseClip = TParameters::TargetActionNoiseClip;
    P.PPOClip = TParameters::PPOClip;
    P.GAE = TParameters::GAE;
    P.PPOEntropyCoefficient = TParameters::PPOEntropyCoefficient;
    P.NormalizeAdvantage = TParameters::NormalizeAdvantage;
    P.InitialActionStd = TParameters::InitialActionStd;
    P.LearnActionStd = TParameters::LearnActionStd;
    return P;
}
std::vector<FProfile> FLearner::GetProfiles()
{
    std::vector<FProfile> Profiles;
    for (EAlgorithm Algorithm : { EAlgorithm::SAC, EAlgorithm::TD3, EAlgorithm::PPO })
    {
        const std::string Prefix = Algorithm == EAlgorithm::SAC ? "sac/" : Algorithm == EAlgorithm::TD3 ? "td3/" : "ppo/";
#define SCHOLA_RLTOOLS_PROFILE(Id, O, A, H, B, R, L, Parameters) Profiles.push_back(Describe<Parameters>(Prefix + Id, Algorithm, O, A, H, B, R, L));
#define SCHOLA_RLTOOLS_ALGORITHM_PROFILE(Kind, Id, O, A, H, B, R, L, Parameters) \
    if (Algorithm == EAlgorithm::Kind) SCHOLA_RLTOOLS_PROFILE(Id,O,A,H,B,R,L,Parameters)
#include "RLToolsProfileRegistry.inl"
#undef SCHOLA_RLTOOLS_PROFILE
#undef SCHOLA_RLTOOLS_ALGORITHM_PROFILE
    }
    return Profiles;
}
FLearner::FLearner(std::unique_ptr<FImpl> InImpl) : Impl(std::move(InImpl)) {}
FLearner::~FLearner()
{
    Stop();
    std::lock_guard Lock(Impl->TrainingMutex);
}
bool FLearner::ValidateSettings(const std::string& ProfileId, const FSettings& Settings, std::string& Error)
{
    auto Profiles = GetProfiles();
    auto Match = std::find_if(Profiles.begin(), Profiles.end(), [&](const FProfile& P) { return P.Id == ProfileId; });
    if (Match == Profiles.end()) { Error = "No compiled RLtools configuration matches this identifier. Generate the configuration and build the Unreal project."; return {}; }
    if (std::count_if(Profiles.begin(), Profiles.end(), [&](const FProfile& P) { return P.Id == ProfileId; }) != 1)
    { Error = "Duplicate compiled RLtools configuration identifier."; return {}; }
    if (!Settings.QueueCapacity) { Error = "QueueCapacity must be greater than zero."; return false; }
    if (!Settings.MaxRecordsPerBurst) { Error = "MaxRecordsPerBurst must be greater than zero."; return false; }
    if (!Settings.TrainFrequency) { Error = "TrainFrequency must be greater than zero."; return false; }
    if (!Settings.GradientSteps) { Error = "GradientSteps must be greater than zero."; return false; }
    if (!Settings.MaxUpdatesPerBurst) { Error = "MaxUpdatesPerBurst must be greater than zero."; return false; }
    if (!Settings.PublishEveryUpdates) { Error = "PublishEveryUpdates must be greater than zero."; return false; }
    if (!Settings.PPOEpochs) { Error = "PPOEpochs must be greater than zero."; return false; }
    if (!std::isfinite(Settings.LearningRate) || Settings.LearningRate <= 0)
    { Error = "LearningRate must be finite and greater than zero."; return false; }
    if (!std::isfinite(Settings.ExplorationNoise) || Settings.ExplorationNoise < 0)
    { Error = "ExplorationNoise must be finite and nonnegative."; return false; }

    if (Match->Algorithm != EAlgorithm::PPO && Settings.LearningStarts < Match->BatchSize)
    { Error = "LearningStarts must be at least the compiled training batch size."; return {}; }
    if ((!Settings.ObservationMean.empty() || !Settings.ObservationStd.empty())
        && (!Finite(Settings.ObservationMean, Match->Observations) || !Finite(Settings.ObservationStd, Match->Observations)
            || std::any_of(Settings.ObservationStd.begin(), Settings.ObservationStd.end(), [](float V) { return V <= 0; })))
    { Error = "Fixed observation normalization requires one finite mean and positive standard deviation per observation."; return {}; }
    Error.clear();
    return true;
}
std::unique_ptr<FLearner> FLearner::Create(const std::string& ProfileId, const FSettings& Settings, std::string& Error)
{
    if (!ValidateSettings(ProfileId, Settings, Error)) return {};
    auto Profiles = GetProfiles();
    const auto Match = std::find_if(Profiles.begin(), Profiles.end(), [&](const FProfile& P) { return P.Id == ProfileId; });
    auto Impl = std::make_unique<FImpl>(*Match, Settings);
    return std::unique_ptr<FLearner>(new FLearner(std::move(Impl)));
}

bool FLearner::Decide(const std::vector<float>& Observation, std::uint64_t Agent, std::uint64_t Episode,
    bool Deterministic, FDecision& OutDecision)
{
    const auto Policy = Impl->LoadPublished();
    const auto& Settings = Policy->Settings;
    auto Context = std::make_shared<FDecisionContext>();
    if (!Policy->Preprocess(Observation, Context->Observation)) return false;
    {
        std::lock_guard Lock(Impl->AdmissionMutex);
        if (!Impl->Stats.Running || Policy->GetGeneration() != Impl->Stats.Generation) return false;
        Context->Sequence = Impl->AgentSequences[Agent]++;
    }
    Context->Policy = Policy;
    Context->Agent = Agent;
    Context->Episode = Episode;
    const unsigned Seed = Settings.Seed ^ static_cast<unsigned>(Agent * 2654435761ULL)
        ^ static_cast<unsigned>(Context->Sequence * 2246822519ULL);
    if (!Policy->Sample(Context->Observation, Deterministic, Seed, Context->RawAction, Context->LogProbability, Context->Value)) return false;
    Context->Action = Context->RawAction;
    std::mt19937 Rng(Seed);
    if (!Deterministic && Policy->GetProfile().Algorithm != EAlgorithm::PPO)
    {
        if (Context->Sequence < Settings.InitialRandomSteps)
            for (float& Value : Context->Action) Value = std::uniform_real_distribution<float>(-1, 1)(Rng);
        else if (Policy->GetProfile().Algorithm == EAlgorithm::TD3 && Settings.ExplorationNoise > 0)
            for (float& Value : Context->Action) Value += std::normal_distribution<float>(0, Settings.ExplorationNoise)(Rng);
    }
    for (float& Value : Context->Action) Value = std::clamp(Value, -1.0f, 1.0f);
    if (!Finite(Context->Action, Policy->GetProfile().Actions)) return false;
    OutDecision.Context = std::move(Context);
    return true;
}

ESubmitResult FLearner::Submit(const FDecision& Decision, const std::vector<float>& ExecutedAction,
    float Reward, const std::vector<float>& NextObservation, bool Terminated, bool Truncated)
{
    auto& I = *Impl;
    std::lock_guard Lock(I.AdmissionMutex);
    if (!I.Stats.Running) return ESubmitResult::NotReady;
    if (!I.Stats.LearningEnabled) { ++I.Stats.Frozen; return ESubmitResult::Frozen; }
    const auto& C = Decision.Context;
    std::vector<float> Next;
    const auto Current = I.LoadPublished();
    if (!C || C->Policy->Owner != I.Owner || C->Policy->GetGeneration() != I.Stats.Generation
        || C->Policy->GetProfile().Id != I.Profile.Id || !std::isfinite(Reward)
        || !Finite(ExecutedAction, I.Profile.Actions)
        || std::any_of(ExecutedAction.begin(), ExecutedAction.end(), [](float V) { return V < -1 || V > 1; })
        || !C->Policy->Preprocess(NextObservation, Next))
    { ++I.Stats.Rejected; return ESubmitResult::Rejected; }
    if (I.Profile.Algorithm == EAlgorithm::PPO)
    {
        if (C->Policy->GetVersion() != Current->GetVersion() || ExecutedAction != C->Action)
        { ++I.Stats.Rejected; return ESubmitResult::Rejected; }
        if (I.PPOUpdating) return ESubmitResult::NotReady;
    }
    if (I.Queue.size() >= I.Settings.QueueCapacity) { ++I.Stats.Dropped; return ESubmitResult::Dropped; }
    if (C->Submitted.exchange(true)) { ++I.Stats.Rejected; return ESubmitResult::Rejected; }
    I.Queue.push_back({C, C->Observation, ExecutedAction, std::move(Next), Reward, Terminated, Truncated});
    ++I.Stats.Accepted;
    I.Stats.Queued = static_cast<unsigned>(I.Queue.size());
    return ESubmitResult::Accepted;
}

bool FLearner::Train()
{
    auto& I = *Impl;
    std::unique_lock TrainingLock(I.TrainingMutex, std::try_to_lock);
    if (!TrainingLock) return true;
    const auto Start = std::chrono::steady_clock::now();
    std::deque<FExperience> Records;
    FStats Local;
    {
        std::lock_guard Lock(I.AdmissionMutex);
        if (!I.Stats.Running || !I.Stats.LearningEnabled) return false;
        I.Stats.Training = true;
        Local = I.Stats;
        unsigned Limit = I.Settings.MaxRecordsPerBurst;
        if (I.Profile.Algorithm == EAlgorithm::PPO)
            Limit = std::min(Limit, I.Profile.RolloutLength - I.State->Occupancy());
        while (!I.Queue.empty() && Records.size() < Limit)
        { Records.push_back(std::move(I.Queue.front())); I.Queue.pop_front(); }
    }
    for (const auto& E : Records) { I.State->Store(E); ++Local.Stored; ++I.RecordsSinceTrain; }
    if (I.Profile.Algorithm == EAlgorithm::PPO && I.State->Ready())
    {
        std::lock_guard Lock(I.AdmissionMutex);
        I.PPOUpdating = true;
        if (!Local.PendingUpdates) Local.PendingUpdates = I.Settings.PPOEpochs;
    }
    else if (I.Profile.Algorithm != EAlgorithm::PPO)
    {
        if (I.State->Ready() && Local.Stored >= I.Settings.LearningStarts)
        {
            Local.PendingUpdates += (I.RecordsSinceTrain / I.Settings.TrainFrequency) * I.Settings.GradientSteps;
            I.RecordsSinceTrain %= I.Settings.TrainFrequency;
        }
        else I.RecordsSinceTrain = 0; // Warm-up does not accumulate an unbounded update debt.
    }
    unsigned Completed = 0;
    while (Local.PendingUpdates && Completed < I.Settings.MaxUpdatesPerBurst)
    {
        {
            std::lock_guard Lock(I.AdmissionMutex);
            if (!I.Stats.Running || !I.Stats.LearningEnabled) break;
        }
        I.State->Update(Local);
        --Local.PendingUpdates;
        ++Completed;
        ++I.UpdatesSincePublish;
        const bool PPOComplete = I.Profile.Algorithm == EAlgorithm::PPO && I.State->IterationComplete();
        if (PPOComplete || (I.Profile.Algorithm != EAlgorithm::PPO && I.UpdatesSincePublish >= I.Settings.PublishEveryUpdates))
        {
            ++Local.Version;
            // Publish outside AdmissionMutex; inference only loads the immutable shared pointer.
            auto Snapshot = I.State->Snapshot();
            Snapshot->Profile = I.Profile;
            Snapshot->Settings = I.Settings;
            Snapshot->Generation = Local.Generation;
            Snapshot->Version = Local.Version;
            Snapshot->Owner = I.Owner;
            I.StorePublished(std::move(Snapshot));
            I.UpdatesSincePublish = 0;
            if (PPOComplete)
            {
                std::lock_guard Lock(I.AdmissionMutex);
                Local.Stale += I.Queue.size();
                I.Queue.clear(); // These outcomes were generated by the previous on-policy actor.
                I.PPOUpdating = false;
            }
        }
    }
    Local.LastBurstMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    {
        std::lock_guard Lock(I.AdmissionMutex);
        // Admission counters may have changed while the training worker ran.
        I.Stats.Stored = Local.Stored;
        I.Stats.Updates = Local.Updates;
        I.Stats.ActorUpdates = Local.ActorUpdates;
        I.Stats.CriticUpdates = Local.CriticUpdates;
        I.Stats.TargetUpdates = Local.TargetUpdates;
        I.Stats.Version = Local.Version;
        I.Stats.Stale = Local.Stale;
        I.Stats.PendingUpdates = Local.PendingUpdates;
        I.Stats.LastBurstMilliseconds = Local.LastBurstMilliseconds;
        I.Stats.Training = false;
        I.Stats.Queued = static_cast<unsigned>(I.Queue.size());
        if (I.Profile.Algorithm == EAlgorithm::PPO) I.Stats.RolloutSize = I.State->Occupancy();
        else I.Stats.ReplaySize = I.State->Occupancy();
        return I.Stats.Running && I.Stats.LearningEnabled && (!I.Queue.empty() || I.Stats.PendingUpdates > 0);
    }
}
std::shared_ptr<const FPolicy> FLearner::CapturePolicy() const { return Impl->LoadPublished(); }
FStats FLearner::ReadStats() const
{
    std::lock_guard Lock(Impl->AdmissionMutex);
    auto Result = Impl->Stats;
    Result.Queued = static_cast<unsigned>(Impl->Queue.size());
    return Result;
}
void FLearner::SetLearningEnabled(bool Enabled)
{
    { std::lock_guard Lock(Impl->AdmissionMutex); Impl->Stats.LearningEnabled = Enabled; }
}
void FLearner::Reset(unsigned Seed)
{
    auto& I = *Impl;
    std::lock_guard TrainingLock(I.TrainingMutex);
    FSettings Settings;
    std::uint64_t Generation;
    std::deque<FExperience> RetiredRecords;
    {
        std::lock_guard AdmissionLock(I.AdmissionMutex);
        if (I.StopRequested) return;
        // Close admission before allocation; tick-time callers never wait for construction.
        I.Stats.Running = false;
        RetiredRecords.swap(I.Queue);
        I.Stats.Queued = 0;
        Settings = I.Settings;
        Settings.Seed = Seed;
        Generation = I.Stats.Generation + 1;
    }
    auto Replacement = MakeState(I.Profile, Settings);
    auto Policy = Replacement->Snapshot();
    Policy->Profile = I.Profile;
    Policy->Settings = Settings;
    Policy->Generation = Generation;
    Policy->Version = 0;
    Policy->Owner = I.Owner;
    std::unique_ptr<FAlgorithmState> RetiredState;
    std::shared_ptr<const FPolicy> RetiredPolicy;
    {
        std::lock_guard AdmissionLock(I.AdmissionMutex);
        if (I.StopRequested) return; // A stop during construction must never reopen the learner.
        RetiredState = std::move(I.State);
        I.State = std::move(Replacement);
        I.Settings = std::move(Settings);
        I.Stats = {};
        I.Stats.Generation = Generation;
        I.RecordsSinceTrain = I.UpdatesSincePublish = 0;
        I.PPOUpdating = false;
        I.AgentSequences.clear();
        RetiredPolicy = I.ExchangePublished(std::move(Policy));
    }
    // Reclaim the old training state and snapshots outside the admission lock.
}
void FLearner::Stop()
{
    if (!Impl) return;
    Impl->StopRequested = true;
    { std::lock_guard Lock(Impl->AdmissionMutex); Impl->Stats.Running = false; }
}
}
