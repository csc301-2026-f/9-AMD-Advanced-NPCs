// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Learning/RLToolsLearner.h"
#include "RLToolsPolicyTypes.h"
#include "Policies/AdaptiveLearningTypes.h"
#include "Common/InteractionDefinition.h"
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

class URLToolsNPCPolicy;

namespace Schola::RLTools
{
enum class EWork : uint8 { Create, Reset, Fence, Train, Dispose, Save, Load };
enum class EBindingPhase : uint8 { Constructing, Ready, Resetting };
struct FCommand
{
    EWork Kind = EWork::Train;
    FGuid Binding, Operation;
    unsigned Seed = 0;
    // Save/Load only: absolute UTF-8 path. Load reads the mode from the file.
    std::string Path;
    ECheckpointMode Mode = ECheckpointMode::TrainingState;
};
/** Native data only. Learner pointer construction/disposal is worker-owned.
 * The game thread reads it only after Ready delivery and before closing the binding.
 * Reset and Load change numerical state, not this pointer, and close all bindings first.
 */
struct FWorkerLearner
{
    FGuid Id = FGuid::NewGuid();
    std::string Profile;
    FSettings Settings;
    std::unique_ptr<FLearner> Learner;
    std::atomic<bool> Stopping{false};
    // Executor mutex protects these scheduling fields.
    std::deque<FCommand> Commands;
    bool Queued = false, Executing = false, NeedsTrain = false;
};

class FWorldExecutor : public std::enable_shared_from_this<FWorldExecutor>
{
public:
    using FCompletion = std::function<void(FGuid, uint8, FGuid, FGuid, const FString&)>;
    FWorldExecutor(int32 Limit, FCompletion Completion);
    std::shared_ptr<FWorkerLearner> Create(std::string Profile, FSettings Settings);
    void Enqueue(const std::shared_ptr<FWorkerLearner>& Learner, FCommand Command);
    void Wake(const std::shared_ptr<FWorkerLearner>& Learner);
    void Release(const std::shared_ptr<FWorkerLearner>& Learner);
    void Close();
    FRLToolsWorldStats Statistics() const;
#if WITH_DEV_AUTOMATION_TESTS
    // Private deterministic worker interleaving seam; never shipped in a non-test build.
    void SetWorkGateForTests(std::function<void(EWork)> Gate);
#endif
private:
    void QueueLocked(const std::shared_ptr<FWorkerLearner>& Learner);
    void DispatchLocked();
    void Execute(std::shared_ptr<FWorkerLearner> Learner, FCommand Command);
    void Notify(const std::shared_ptr<FWorkerLearner>& Learner, const FCommand& Command, const FString& Error);
    mutable std::mutex Mutex;
    TMap<FGuid, std::shared_ptr<FWorkerLearner>> Owned;
    std::deque<std::shared_ptr<FWorkerLearner>> Queue;
    FCompletion Completion;
    int32 Limit, Active = 0, Peak = 0, Bursts = 0, Disposed = 0;
    bool Closed = false;
#if WITH_DEV_AUTOMATION_TESTS
    std::function<void(EWork)> TestGate;
#endif
};
struct FRetainedDecision
{
    FDecision Native;
    std::vector<float> Observation;
    TArray<float> Command;
    int64 Episode = 0;
    double Deadline = 0;
    bool Retrieved = false;
};
struct FWorldBinding
{
    TWeakObjectPtr<URLToolsNPCPolicy> Policy;
    FGuid Learner;
    int64 Agent = 0, Episode = 0;
    int32 Capacity = 0;
    double Retention = 0;
    TMap<int64, FRetainedDecision> Decisions;
};
struct FWorldLearner
{
    std::shared_ptr<FWorkerLearner> Worker;
    FInteractionDefinition Definition;
    FRLToolsRuntimeSettings Settings;
    FString Profile;
    ERLToolsAlgorithm Algorithm = ERLToolsAlgorithm::SAC;
    FName Group;
    FGuid Session = FGuid::NewGuid();
    EBindingPhase Phase = EBindingPhase::Constructing;
    TArray<FGuid> Bindings;
    // Pending Save/Load destination, reported in its completion diagnostic.
    FString Checkpoint;
    bool ControlPending = false;
};
struct FWorldService
{
    std::shared_ptr<FWorldExecutor> Executor;
    // Only the game thread touches these maps, UObject weak handles and Schola records.
    TMap<FGuid, FWorldLearner> Learners;
    TMap<FGuid, FWorldBinding> Bindings;
    TMap<FName, FGuid> Groups;
    int64 NextAgent = 0;

    FWorldLearner* Ready(FGuid Binding, FString& Error);
    void Prune(FWorldBinding& Binding);
};
}
