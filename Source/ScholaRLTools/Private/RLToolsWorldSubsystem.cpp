// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "RLToolsWorldSubsystem.h"
#include "Policies/RLToolsNPCPolicy.h"
#include "Policies/RLToolsPolicySnapshot.h"
#include "Learning/RLToolsWorldRuntime.h"
#include "Learning/RLToolsPolicyConversion.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Spaces/BoxSpace.h"

using namespace Schola::RLTools;

namespace
{
// Empty, directory-only or relative paths resolve under Saved/Schola/Learning; the worker creates the directory.
FString CheckpointPath(const FString& Path, const FString& Profile)
{
    FString Result = Path;
    if (Result.IsEmpty() || Result.EndsWith(TEXT("/")) || Result.EndsWith(TEXT("\\")))
        Result += FPaths::MakeValidFileName(Profile, TEXT('_')) + TEXT(".ckpt");
    if (FPaths::IsRelative(Result)) Result = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Schola/Learning"), Result);
    return FPaths::ConvertRelativePathToFull(Result);
}
bool CheckpointMode(ERLToolsCheckpointMode Mode, ECheckpointMode& Out)
{
    switch (Mode)
    {
    case ERLToolsCheckpointMode::PolicyExport: Out = ECheckpointMode::PolicyExport; return true;
    case ERLToolsCheckpointMode::TrainingState: Out = ECheckpointMode::TrainingState; return true;
    case ERLToolsCheckpointMode::FullResume: Out = ECheckpointMode::FullResume; return true;
    default: return false;
    }
}
}

bool URLToolsWorldSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE || Type == EWorldType::GamePreview;
}
void URLToolsWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bActive = true;
    Runtime = std::make_shared<FWorldService>();
    const TWeakObjectPtr<URLToolsWorldSubsystem> Weak(this);
    Runtime->Executor = std::make_shared<FWorldExecutor>(FMath::Clamp(MaximumConcurrentWorkers, 1, 8),
        [Weak](FGuid Learner, uint8 Kind, FGuid Binding, FGuid Operation, const FString& Error)
        {
            check(IsInGameThread());
            if (auto* Service = Weak.Get()) Service->HandleCompletion(Learner, Kind, Binding, Operation, Error);
        });
}
void URLToolsWorldSubsystem::Deinitialize()
{
    check(IsInGameThread());
    bActive = false;
    if (Runtime)
    {
        TArray<FGuid> Ids;
        Runtime->Bindings.GetKeys(Ids);
        for (FGuid Id : Ids)
        {
            auto* Binding = Runtime->Bindings.Find(Id);
            auto* Policy = Binding ? Binding->Policy.Get() : nullptr;
            if (Policy && Policy->BindingId == Id) Policy->Shutdown();
            else Release(Id);
        }
        Runtime->Executor->Close();
        Runtime->Bindings.Empty();
        Runtime->Learners.Empty();
        Runtime->Groups.Empty();
    }
    Super::Deinitialize();
}
FRLToolsWorldStats URLToolsWorldSubsystem::GetExecutionStats() const
{
    check(IsInGameThread());
    if (!Runtime) return {};
    auto Stats = Runtime->Executor->Statistics();
    Stats.Learners = Runtime->Learners.Num();
    Stats.Bindings = Runtime->Bindings.Num();
    return Stats;
}
bool URLToolsWorldSubsystem::ValidateBinding(const URLToolsNPCPolicy& Policy, const FInteractionDefinition& Definition,
    FGuid Existing, FString& Error) const
{
    check(IsInGameThread());
    if (!bActive || !Runtime || Runtime->NextAgent == MAX_int64)
    {
        Error = TEXT("A live game/PIE world with available agent identities is required.");
        return false;
    }
    ERLToolsAlgorithm Algorithm;
    FString Configuration;
    if (!Policy.ResolveConfiguration(Algorithm, Configuration, Error)) return false;
    if (Policy.SharedGroup.IsNone()) return true;
    const auto* GroupId = Runtime->Groups.Find(Policy.SharedGroup);
    const auto* Group = GroupId ? Runtime->Learners.Find(*GroupId) : nullptr;
    if (!Group || (Group->Bindings.Num() == 1 && Group->Bindings[0] == Existing)) return true;
    if (Group->Phase == EBindingPhase::Resetting)
    {
        Error = TEXT("Shared learner is resetting. Wait for completion before joining.");
        return false;
    }
    if (Group->Algorithm != Algorithm || Group->Profile != Configuration
        || !SameSettings(Group->Settings, Policy.Settings) || !SameDefinition(Group->Definition, Definition))
    {
        Error = FString::Printf(TEXT("Shared group '%s' is incompatible: algorithm, compiled configuration, full observation/action definitions (including bounds/shape and AgentType), preprocessing and all learner-wide settings must match."),
            *Policy.SharedGroup.ToString());
        return false;
    }
    return true;
}
bool URLToolsWorldSubsystem::Bind(URLToolsNPCPolicy& Policy, const FInteractionDefinition& Definition, FGuid& Out, FString& Error)
{
    check(IsInGameThread());
    if (!ValidateBinding(Policy, Definition, FGuid(), Error)) return false;
    ERLToolsAlgorithm Algorithm;
    FString Configuration;
    if (!Policy.ResolveConfiguration(Algorithm, Configuration, Error)) return false;
    FGuid LearnerId;
    if (const auto* Group = Runtime->Groups.Find(Policy.SharedGroup); !Policy.SharedGroup.IsNone() && Group)
        LearnerId = *Group;
    else
    {
        FWorldLearner Entry;
        Entry.Profile = Configuration;
        Entry.Algorithm = Algorithm;
        Entry.Definition = Definition;
        Entry.Settings = Policy.Settings;
        Entry.Group = Policy.SharedGroup;
        Entry.Worker = Runtime->Executor->Create(TCHAR_TO_UTF8(*Configuration), ToNative(Policy.Settings));
        LearnerId = Entry.Worker->Id;
        Runtime->Learners.Add(LearnerId, MoveTemp(Entry));
        if (!Policy.SharedGroup.IsNone()) Runtime->Groups.Add(Policy.SharedGroup, LearnerId);
    }
    Out = FGuid::NewGuid();
    FWorldBinding Binding;
    Binding.Policy = &Policy;
    Binding.Learner = LearnerId;
    Binding.Agent = Runtime->NextAgent++;
    Binding.Capacity = Policy.DecisionCapacity;
    Binding.Retention = Policy.ExperienceRetentionSeconds;
    Runtime->Bindings.Add(Out, MoveTemp(Binding));
    auto& Learner = Runtime->Learners.FindChecked(LearnerId);
    Learner.Bindings.Add(Out);
    if (Learner.Phase == EBindingPhase::Ready) Policy.BindingReady(Out, Learner.Session, Runtime->Bindings.FindChecked(Out).Agent);
    return true;
}
void URLToolsWorldSubsystem::Release(FGuid Id)
{
    check(IsInGameThread());
    if (!Runtime) return;
    auto* Binding = Runtime->Bindings.Find(Id);
    if (!Binding) return;
    const FGuid LearnerId = Binding->Learner;
    Runtime->Bindings.Remove(Id);
    auto* Entry = Runtime->Learners.Find(LearnerId);
    if (!Entry) return;
    Entry->Bindings.Remove(Id);
    if (!Entry->Bindings.IsEmpty()) return;
    // Before creation's completion is delivered we must not read its worker-written pointer.
    if (Entry->Phase != EBindingPhase::Constructing && Entry->Worker->Learner) Entry->Worker->Learner->Stop();
    Runtime->Executor->Release(Entry->Worker);
    if (!Entry->Group.IsNone()) Runtime->Groups.Remove(Entry->Group);
    Runtime->Learners.Remove(LearnerId);
}
void URLToolsWorldSubsystem::HandleCompletion(FGuid Id, uint8 KindValue, FGuid Initiator, FGuid Operation, const FString& Error)
{
    check(IsInGameThread());
    if (!bActive || !Runtime) return;
    auto* Entry = Runtime->Learners.Find(Id);
    if (!Entry) return;
    const EWork Kind = static_cast<EWork>(KindValue);
    if (Kind == EWork::Create || Kind == EWork::Reset)
    {
        const auto Expected = Kind == EWork::Create ? EBindingPhase::Constructing : EBindingPhase::Resetting;
        if (Entry->Phase != Expected) return;
        Entry->ControlPending = false;
        Entry->Phase = EBindingPhase::Ready;
        const FGuid Session = Entry->Session;
        const TArray<FGuid> Bindings = Entry->Bindings;
        for (FGuid BindingId : Bindings)
        {
            auto* Binding = Runtime->Bindings.Find(BindingId);
            auto* Policy = Binding ? Binding->Policy.Get() : nullptr;
            if (!Policy) { Release(BindingId); continue; }
            if (Error.IsEmpty()) Policy->BindingReady(BindingId, Session, Binding->Agent);
            else Policy->BindingFailed(BindingId, Error);
        }
    }
    else if (Kind == EWork::Fence) Entry->ControlPending = false;
    FString Diagnostic = Error;
    if (Kind == EWork::Save || Kind == EWork::Load)
    {
        if (Kind == EWork::Load && Entry->Phase != EBindingPhase::Resetting) return;
        Entry->ControlPending = false;
        Diagnostic = Error.IsEmpty()
            ? FString::Printf(TEXT("%s %s."), Kind == EWork::Save ? TEXT("Saved checkpoint to") : TEXT("Loaded checkpoint from"), *Entry->Checkpoint)
            : FString::Printf(TEXT("%s (%s)%s"), *Error, *Entry->Checkpoint, Kind == EWork::Load ? TEXT(". Learner, session and decisions unchanged.") : TEXT(""));
        if (!Entry->Group.IsNone())
            Diagnostic += FString::Printf(TEXT(" Shared learner '%s': %s %d bindings."), *Entry->Group.ToString(),
                Error.IsEmpty() ? TEXT("affected") : TEXT("did not change"), Entry->Bindings.Num());
        Entry->Checkpoint.Reset();
    }
    if (Kind == EWork::Load)
    {
        // Failure keeps the same session: facades were never suspended and retained decisions stay valid.
        Entry->Phase = EBindingPhase::Ready;
        if (Error.IsEmpty())
        {
            // Success matches Reset: native generation advanced, so rotate the session and invalidate every member.
            Entry->Session = FGuid::NewGuid();
            const FGuid Session = Entry->Session;
            const TArray<FGuid> Bindings = Entry->Bindings;
            for (FGuid BindingId : Bindings)
            {
                auto* Binding = Runtime->Bindings.Find(BindingId);
                auto* Policy = Binding ? Binding->Policy.Get() : nullptr;
                if (!Policy) { Release(BindingId); continue; }
                const int64 Agent = Binding->Agent;
                Binding->Decisions.Empty();
                Binding->Episode = 0;
                Policy->BindingResetting(BindingId);
                Policy->BindingReady(BindingId, Session, Agent);
            }
        }
    }
    if (Initiator.IsValid())
    {
        auto* Binding = Runtime->Bindings.Find(Initiator);
        if (auto* Policy = Binding ? Binding->Policy.Get() : nullptr)
            Policy->BindingOperationCompleted(Initiator, Operation, Error, Diagnostic);
    }
}
bool URLToolsWorldSubsystem::Decide(FGuid Id, const FAdaptiveDecisionRequest& Request, EAdaptiveInferenceMode Mode,
    FAdaptiveDecision& Decision, FString& Error)
{
    auto* Entry = Runtime->Ready(Id, Error);
    if (!Entry) return false;
    auto& Binding = Runtime->Bindings.FindChecked(Id);
    if (Request.SessionId != Entry->Session || Request.AgentId != Binding.Agent || Request.EpisodeId != Binding.Episode)
    {
        Error = TEXT("Decision attribution does not match the live binding.");
        return false;
    }
    Runtime->Prune(Binding);
    std::vector<float> Input;
    FDecision Native;
    if (!ObservationVector(Request.Observation, Entry->Definition, Input)
        || !Entry->Worker->Learner->Decide(Input, Binding.Agent, Binding.Episode,
            Mode == EAdaptiveInferenceMode::Deterministic, Native))
    {
        Error = TEXT("Native inference is unavailable or the observation cannot be preprocessed.");
        return false;
    }
    if (!ActionPoint(Native.GetAction(), Entry->Definition, Decision.Action))
    {
        Error = TEXT("Native action does not match the declared bounded Box space.");
        return false;
    }
    Decision.SessionId = Entry->Session;
    Decision.AgentId = Binding.Agent;
    Decision.EpisodeId = Binding.Episode;
    Decision.DecisionId = Request.DecisionId;
    Decision.PolicyVersion = static_cast<int64>(Native.GetVersion());
    while (Binding.Decisions.Num() >= Binding.Capacity)
    {
        int64 Oldest = MAX_int64;
        for (const auto& Pair : Binding.Decisions) Oldest = FMath::Min(Oldest, Pair.Key);
        Binding.Decisions.Remove(Oldest);
    }
    FRetainedDecision Record;
    Record.Native = std::move(Native);
    Record.Observation = std::move(Input);
    Record.Command = Decision.Action.Get<FBoxPoint>().Values;
    Record.Episode = Binding.Episode;
    Record.Deadline = FPlatformTime::Seconds() + Binding.Retention;
    Binding.Decisions.Add(Request.DecisionId, MoveTemp(Record));
    return true;
}
EAdaptiveSubmitResult URLToolsWorldSubsystem::Submit(FGuid Id, const FAdaptiveExperience& Experience, FString& Error)
{
    auto* Entry = Runtime->Ready(Id, Error);
    if (!Entry) return EAdaptiveSubmitResult::NotReady;
    auto& Binding = Runtime->Bindings.FindChecked(Id);
    Runtime->Prune(Binding);
    auto* Record = Binding.Decisions.Find(Experience.DecisionId);
    if (!Record)
    {
        Error = TEXT("Unknown, already submitted, cancelled or retired native decision. Retention is bounded by capacity and time.");
        return EAdaptiveSubmitResult::Rejected;
    }
    if (Experience.SessionId != Entry->Session || Experience.AgentId != Binding.Agent
        || Experience.EpisodeId != Binding.Episode || Record->Episode != Binding.Episode
        || Experience.PolicyVersion != static_cast<int64>(Record->Native.GetVersion())
        || Record->Native.GetGeneration() != Entry->Worker->Learner->ReadStats().Generation)
    {
        Error = TEXT("Stale native decision or altered session, agent, episode, generation or policy-version attribution.");
        return EAdaptiveSubmitResult::Rejected;
    }
    if (!Record->Retrieved)
    {
        Error = TEXT("The native decision must be retrieved before gameplay executes it.");
        return EAdaptiveSubmitResult::Rejected;
    }
    std::vector<float> Observation, Next;
    if (!ObservationVector(Experience.Observation, Entry->Definition, Observation) || Observation != Record->Observation)
    {
        Error = TEXT("Experience observation does not match the observation that originated this native decision.");
        return EAdaptiveSubmitResult::Rejected;
    }
    if (!ObservationVector(Experience.NextObservation, Entry->Definition, Next))
    {
        Error = TEXT("Next observation does not match the declared observation space.");
        return EAdaptiveSubmitResult::Rejected;
    }
    const auto& Command = Experience.ExecutedAction.Get<FBoxPoint>().Values;
    const bool Unchanged = Command == Record->Command;
    if (Entry->Algorithm == ERLToolsAlgorithm::PPO && !Unchanged)
    {
        Error = TEXT("PPO cannot train on an additional gameplay modification of its returned command. Submit the unchanged action or cancel this decision.");
        return EAdaptiveSubmitResult::Rejected;
    }
    // Exact command reuse avoids losing PPO's original normalized bits in a scaling round trip.
    const auto Executed = Unchanged ? Record->Native.GetAction() : NormalizeAction(Experience.ExecutedAction, Entry->Definition);
    const ESubmitResult Result = Entry->Worker->Learner->Submit(Record->Native, Executed, Experience.Reward,
        Next, Experience.bTerminated, Experience.bTruncated);
    switch (Result)
    {
    case ESubmitResult::Accepted:
        Binding.Decisions.Remove(Experience.DecisionId);
        Runtime->Executor->Wake(Entry->Worker);
        return EAdaptiveSubmitResult::Accepted;
    case ESubmitResult::Dropped:
        Error = TEXT("Learner queue is full. The native decision remains retained for retry within its retention limit.");
        return EAdaptiveSubmitResult::Dropped;
    case ESubmitResult::Frozen:
        return EAdaptiveSubmitResult::Frozen;
    case ESubmitResult::NotReady:
        Error = TEXT("Learner admission is temporarily unavailable during an update. The decision remains retained for retry; PPO may retire it when the policy version changes.");
        return EAdaptiveSubmitResult::NotReady;
    default:
        Error = Entry->Algorithm == ERLToolsAlgorithm::PPO
            ? TEXT("PPO decision is stale after policy publication; request a decision from the current policy.")
            : TEXT("Native decision was rejected as stale or previously admitted.");
        return EAdaptiveSubmitResult::Rejected;
    }
}
void URLToolsWorldSubsystem::Retrieved(FGuid Id, int64 Decision)
{
    auto* Binding = Runtime ? Runtime->Bindings.Find(Id) : nullptr;
    auto* Record = Binding ? Binding->Decisions.Find(Decision) : nullptr;
    if (Record && !Record->Retrieved)
    {
        Record->Retrieved = true;
        Record->Deadline = FPlatformTime::Seconds() + Binding->Retention;
    }
}
bool URLToolsWorldSubsystem::Retire(FGuid Id, int64 Decision, bool bCancelled)
{
    auto* Binding = Runtime ? Runtime->Bindings.Find(Id) : nullptr;
    auto* Record = Binding ? Binding->Decisions.Find(Decision) : nullptr;
    if (!Record || (!bCancelled && Record->Retrieved)) return false;
    Binding->Decisions.Remove(Decision);
    return true;
}
void URLToolsWorldSubsystem::EpisodeReset(FGuid Id, int64 Episode)
{
    if (auto* Binding = Runtime ? Runtime->Bindings.Find(Id) : nullptr)
    {
        Binding->Decisions.Empty();
        Binding->Episode = Episode;
    }
}
bool URLToolsWorldSubsystem::LearningControl(FGuid Id, bool bEnabled, FGuid Operation, FString& Error)
{
    auto* Entry = Runtime->Ready(Id, Error);
    if (!Entry) return false;
    if (Entry->ControlPending)
    {
        Error = TEXT("A shared learner control operation is still pending. Wait for its operation result.");
        return false;
    }
    Entry->ControlPending = true;
    Entry->Worker->Learner->SetLearningEnabled(bEnabled); // Short admission lock; never waits for a numerical update.
    Runtime->Executor->Enqueue(Entry->Worker, {EWork::Fence, Id, Operation});
    return true;
}
bool URLToolsWorldSubsystem::LearningReset(FGuid Id, int32 Seed, FGuid Operation, FString& Error)
{
    auto* Entry = Runtime->Ready(Id, Error);
    if (!Entry) return false;
    if (Seed < 0 || Entry->ControlPending)
    {
        Error = TEXT("Reset requires a nonnegative seed and no pending shared learner control operation.");
        return false;
    }
    Entry->ControlPending = true;
    Entry->Phase = EBindingPhase::Resetting;
    Entry->Session = FGuid::NewGuid();
    Entry->Settings.Seed = Seed;
    Entry->Worker->Learner->SetLearningEnabled(false);
    const TArray<FGuid> Bindings = Entry->Bindings;
    for (FGuid BindingId : Bindings)
    {
        auto& Binding = Runtime->Bindings.FindChecked(BindingId);
        Binding.Decisions.Empty();
        Binding.Episode = 0;
        if (auto* Policy = Binding.Policy.Get()) Policy->BindingResetting(BindingId);
    }
    Runtime->Executor->Enqueue(Entry->Worker, {EWork::Reset, Id, Operation, static_cast<unsigned>(Seed)});
    return true;
}
bool URLToolsWorldSubsystem::LearningSave(FGuid Id, const FString& Path, ERLToolsCheckpointMode Mode, FGuid Operation, FString& Error)
{
    auto* Entry = Runtime->Ready(Id, Error);
    if (!Entry) return false;
    FCommand Command;
    if (!CheckpointMode(Mode, Command.Mode) || Entry->ControlPending)
    {
        Error = TEXT("Save requires a valid checkpoint mode and no pending shared learner control operation.");
        return false;
    }
    // Bindings stay Ready: inference continues while the worker pauses training to serialize.
    Entry->ControlPending = true;
    Entry->Checkpoint = CheckpointPath(Path, Entry->Profile);
    Command.Kind = EWork::Save;
    Command.Binding = Id;
    Command.Operation = Operation;
    Command.Path = TCHAR_TO_UTF8(*Entry->Checkpoint);
    Runtime->Executor->Enqueue(Entry->Worker, MoveTemp(Command));
    return true;
}
bool URLToolsWorldSubsystem::LearningLoad(FGuid Id, const FString& Path, FGuid Operation, FString& Error)
{
    auto* Entry = Runtime->Ready(Id, Error);
    if (!Entry) return false;
    if (Entry->ControlPending)
    {
        Error = TEXT("Load requires no pending shared learner control operation.");
        return false;
    }
    // The whole group stops deciding/submitting; facades are suspended only if the load succeeds.
    Entry->ControlPending = true;
    Entry->Phase = EBindingPhase::Resetting;
    Entry->Checkpoint = CheckpointPath(Path, Entry->Profile);
    FCommand Command;
    Command.Kind = EWork::Load;
    Command.Binding = Id;
    Command.Operation = Operation;
    Command.Path = TCHAR_TO_UTF8(*Entry->Checkpoint);
    Runtime->Executor->Enqueue(Entry->Worker, MoveTemp(Command));
    return true;
}
bool URLToolsWorldSubsystem::Snapshot(FGuid Id, UAdaptivePolicySnapshot*& Out, FString& Error)
{
    auto* Entry = Runtime->Ready(Id, Error);
    if (!Entry) return false;
    auto* Snapshot = NewObject<URLToolsPolicySnapshot>();
    if (!Snapshot->Configure(Entry->Session, Entry->Definition, Entry->Worker->Learner->CapturePolicy()))
    {
        Error = TEXT("Could not capture immutable native inference state.");
        return false;
    }
    Out = Snapshot;
    return true;
}
bool URLToolsWorldSubsystem::Statistics(FGuid Id, FRLToolsLearningStats& Out) const
{
    FString Error;
    auto* Entry = Runtime ? Runtime->Ready(Id, Error) : nullptr;
    if (!Entry) return false;
    const auto Stats = Entry->Worker->Learner->ReadStats();
    FRLToolsLearningStats Result;
    Result.Accepted = Stats.Accepted;
    Result.Rejected = Stats.Rejected;
    Result.Dropped = Stats.Dropped;
    Result.Frozen = Stats.Frozen;
    Result.Stored = Stats.Stored;
    Result.Updates = Stats.Updates;
    Result.ActorUpdates = Stats.ActorUpdates;
    Result.CriticUpdates = Stats.CriticUpdates;
    Result.TargetUpdates = Stats.TargetUpdates;
    Result.Stale = Stats.Stale;
    Result.Version = Stats.Version;
    Result.Generation = Stats.Generation;
    Result.PendingUpdates = Stats.PendingUpdates;
    Result.Queued = Stats.Queued;
    Result.ReplaySize = Stats.ReplaySize;
    Result.RolloutSize = Stats.RolloutSize;
    Result.LastBurstMilliseconds = Stats.LastBurstMilliseconds;
    Result.bLearningEnabled = Stats.LearningEnabled;
    Result.bRunning = Stats.Running;
    Result.bTraining = Stats.Training;
    Result.RetainedDecisions = Runtime->Bindings.FindChecked(Id).Decisions.Num();
    Out = Result;
    return true;
}
bool URLToolsWorldSubsystem::ActiveSettings(FGuid Id, FRLToolsRuntimeSettings& Out) const
{
    auto* Binding = Runtime ? Runtime->Bindings.Find(Id) : nullptr;
    auto* Entry = Binding ? Runtime->Learners.Find(Binding->Learner) : nullptr;
    if (!Entry) return false;
    Out = Entry->Settings;
    return true;
}
