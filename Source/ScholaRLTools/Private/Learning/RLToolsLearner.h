// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/** Internal, engine-independent API used by the ScholaRLTools policy/service implementation.
 * All algorithm headers stay in RLToolsLearner.cpp. Game code uses Schola points and policies.
 */
namespace Schola::RLTools
{
enum class EAlgorithm { SAC, TD3, PPO };
enum class ESubmitResult { Accepted, Rejected, Dropped, Frozen, NotReady };

/** The complete structural configuration actually compiled into this binary. */
struct FProfile
{
    std::string Id;
    EAlgorithm Algorithm;
    unsigned Observations, Actions, HiddenWidth, BatchSize, ReplayCapacity, RolloutLength;
    unsigned ActorLayers = 3, CriticLayers = 3;
    float Discount = 0.99f;
    float TargetTau = 0.005f, EntropyCoefficient = 0.5f;
    unsigned ActorInterval = 1, TargetInterval = 1;
    bool AdaptiveEntropy = true;
    float TargetActionNoise = 0.2f, TargetActionNoiseClip = 0.5f;
    float PPOClip = 0.2f, GAE = 0.95f, PPOEntropyCoefficient = 0.01f;
    float InitialActionStd = 0.5f;
    bool NormalizeAdvantage = true, LearnActionStd = true;
    bool Recurrent = false;
    const char* Device = "CPU";
    const char* Revision = "b32d9985c65a5e098a6bbf190fd994962d288b99";
};

/** Runtime options; network/batch/replay structure is selected with FProfile. */
struct FSettings
{
    unsigned Seed = 1;
    unsigned QueueCapacity = 1024;
    unsigned MaxRecordsPerBurst = 64;
    unsigned LearningStarts = 32;
    unsigned InitialRandomSteps = 32;
    unsigned TrainFrequency = 1;
    unsigned GradientSteps = 1;
    unsigned MaxUpdatesPerBurst = 4;
    unsigned PublishEveryUpdates = 4;
    unsigned PPOEpochs = 4;
    float LearningRate = 0.0003f;
    float ExplorationNoise = 0.1f;
    // Empty means identity. Fixed preprocessing is copied into every policy snapshot.
    std::vector<float> ObservationMean, ObservationStd;
};

struct FStats
{
    std::uint64_t Accepted = 0, Rejected = 0, Dropped = 0, Frozen = 0;
    // Updates counts SAC/TD3 training cycles or PPO epochs. Actor/CriticUpdates count optimizer steps.
    std::uint64_t Stored = 0, Updates = 0, ActorUpdates = 0, CriticUpdates = 0;
    std::uint64_t TargetUpdates = 0, Stale = 0, Version = 0, Generation = 1;
    std::uint64_t PendingUpdates = 0;
    unsigned Queued = 0, ReplaySize = 0, RolloutSize = 0;
    double LastBurstMilliseconds = 0;
    bool LearningEnabled = true, Running = true, Training = false;
};

class FPolicy;
struct FDecisionContext;

/** Opaque retained policy/action information. Callers cannot fabricate training metadata. */
class FDecision
{
public:
    const std::vector<float>& GetAction() const;
    std::uint64_t GetVersion() const;
    std::uint64_t GetGeneration() const;
    explicit operator bool() const { return static_cast<bool>(Context); }
private:
    friend class FLearner;
    std::shared_ptr<FDecisionContext> Context;
};

/** Immutable actor and preprocessing; PPO additionally retains the value function. */
class FPolicy
{
public:
    virtual ~FPolicy() = default;
    // Deterministic evaluation never advances the learner's RNG, counters or training state.
    bool Evaluate(const std::vector<float>& Observation, std::vector<float>& Action) const;
    const FProfile& GetProfile() const { return Profile; }
    std::uint64_t GetVersion() const { return Version; }
    std::uint64_t GetGeneration() const { return Generation; }
    // Internal PPO bootstrap evaluation on an already preprocessed observation.
    bool EvaluatePrepared(const std::vector<float>& Observation, float& Value) const;
protected:
    friend class FLearner;
    friend struct FDecisionContext;
    FProfile Profile;
    FSettings Settings;
    std::uint64_t Version = 0, Generation = 1;
    std::shared_ptr<const void> Owner;
    virtual bool Sample(const std::vector<float>& Observation, bool Deterministic, unsigned Seed,
        std::vector<float>& RawAction, float& LogProbability, float& Value) const = 0;
    bool Preprocess(const std::vector<float>& Observation, std::vector<float>& Result) const;
};

/** One numerical learner, independent of UObjects, game simulation and example modules.
 * Decide/Submit/CapturePolicy/ReadStats may run concurrently with Train.
 * Train is incremental and serialized internally; the world executor chooses its worker.
 * No private thread is created for an NPC. Pause/Stop requests return promptly;
 * ReadStats().Training becomes false when the active update has finished.
 */
class FLearner
{
public:
    ~FLearner();
    static std::vector<FProfile> GetProfiles();
    static std::unique_ptr<FLearner> Create(const std::string& ProfileId, const FSettings& Settings, std::string& Error);
    bool Decide(const std::vector<float>& Observation, std::uint64_t Agent, std::uint64_t Episode,
        bool Deterministic, FDecision& OutDecision);
    ESubmitResult Submit(const FDecision& Decision, const std::vector<float>& ExecutedAction,
        float Reward, const std::vector<float>& NextObservation, bool Terminated, bool Truncated);
    // Returns true if another bounded burst can make progress. Never call on the game thread.
    bool Train();
    std::shared_ptr<const FPolicy> CapturePolicy() const;
    FStats ReadStats() const;
    void SetLearningEnabled(bool Enabled);
    void Reset(unsigned Seed); // Worker-only replacement; old decisions are retired.
    void Stop();
private:
    struct FImpl;
    explicit FLearner(std::unique_ptr<FImpl> Impl);
    std::unique_ptr<FImpl> Impl;
};
}
