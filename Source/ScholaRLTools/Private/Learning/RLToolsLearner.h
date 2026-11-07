// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "RLToolsNetworkTypes.h"

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
    unsigned CriticHiddenWidth = 32;
    EActivation ActorActivation = EActivation::ReLU, CriticActivation = EActivation::ReLU;
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

/** What a checkpoint file contains (spec section 9). The mode is recorded in the file; Load restores
 * exactly what the file holds and nothing more.
 * PolicyExport:  actor (+ PPO value function; for SAC this includes the entropy temperature) and the
 *                policy version. Load keeps freshly initialized critics/optimizer state and an empty
 *                replay, i.e. "start from pretrained weights".
 * TrainingState: every network, target network, optimizer (incl. SAC entropy temperature), the training
 *                RNG and update/version counters. The replay buffer / PPO rollout is NOT included:
 *                after Load the learner refills it and warms up again. This is NOT a full resume.
 * FullResume:    TrainingState plus the off-policy replay buffer contents, so training continues
 *                where it stopped. PPO has no persistent replay; its pending rollout is discarded and
 *                FullResume is equivalent to TrainingState for PPO (FCheckpointInfo::ReplaySize is 0).
 * Preprocessing values are not stored; a fingerprint is, and must match the loading learner.
 * After any successful load, SAC/TD3 skip InitialRandomSteps: the restored actor acts immediately.
 */
enum class ECheckpointMode : std::uint32_t { PolicyExport = 1, TrainingState = 2, FullResume = 3 };

enum class ECheckpointResult
{
    Ok,
    IoError,     // missing/unreadable/unwritable file
    BadFormat,   // not a Schola RLtools checkpoint, truncated, corrupt, wrong checksum, unknown format version
    Mismatch,    // valid file for a different profile/algorithm/dimensions/revision/preprocessing/structure
    Unsupported, // valid request this build cannot satisfy
    NotReady     // learner stopped
};

/** Copied metadata from a checkpoint header; available without loading any numerical state. */
struct FCheckpointInfo
{
    std::uint32_t Format = 0;
    ECheckpointMode Mode = ECheckpointMode::TrainingState;
    std::string ProfileId, Revision;
    EAlgorithm Algorithm = EAlgorithm::SAC;
    unsigned Observations = 0, Actions = 0;
    std::uint64_t Version = 0, Updates = 0, ReplaySize = 0;
    std::uint64_t PayloadSize = 0;
    bool PreprocessingIncluded = false;
};

const char* ToString(ECheckpointResult Result);

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
    // Validate without constructing numerical state; shared by Unreal and native construction.
    static bool ValidateSettings(const std::string& ProfileId, const FSettings& Settings, std::string& Error);
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
    /** Worker-only, like Reset: waits for any active Train burst, never call on the game thread.
     * Writes Path + ".tmp" and renames it over Path, so a failed save never leaves a partial file. */
    ECheckpointResult SaveCheckpoint(const std::string& Path, ECheckpointMode Mode, std::string& Error);
    /** Worker-only. Validates header, sizes (before allocating the payload), checksum, profile,
     * revision, preprocessing and tar structure, then deserializes into a NEW replacement state.
     * On any failure the live learner is unchanged and keeps running. On success the replacement is
     * swapped in exactly like Reset: Generation increments, queued records and all outstanding
     * decisions are retired, and Version becomes max(current + 1, saved) so it never goes backwards. */
    ECheckpointResult LoadCheckpoint(const std::string& Path, std::string& Error, FCheckpointInfo* OutInfo = nullptr);
    /** Reads and validates only the header (and file size); no numerical state is touched. */
    static ECheckpointResult ReadCheckpointInfo(const std::string& Path, FCheckpointInfo& OutInfo, std::string& Error);
    void Stop();
private:
    struct FImpl;
    explicit FLearner(std::unique_ptr<FImpl> Impl);
    std::unique_ptr<FImpl> Impl;
};
}
