// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Learning/RLToolsWorldRuntime.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

namespace Schola::RLTools
{
namespace
{
FString CheckpointError(const TCHAR* Prefix, ECheckpointResult Result, const std::string& Detail)
{
    FString Text = FString::Printf(TEXT("%s: %s"), Prefix, UTF8_TO_TCHAR(ToString(Result)));
    if (!Detail.empty()) Text += FString(TEXT(" - ")) + UTF8_TO_TCHAR(Detail.c_str());
    return Text;
}
}
FWorldExecutor::FWorldExecutor(int32 InLimit, FCompletion InCompletion)
    : Completion(std::move(InCompletion)), Limit(InLimit) {}

std::shared_ptr<FWorkerLearner> FWorldExecutor::Create(std::string Profile, FSettings Settings)
{
    auto Slot = std::make_shared<FWorkerLearner>();
    Slot->Profile = std::move(Profile);
    Slot->Settings = std::move(Settings);
    std::lock_guard Lock(Mutex);
    check(!Closed);
    Owned.Add(Slot->Id, Slot);
    Slot->Commands.push_back({EWork::Create});
    QueueLocked(Slot);
    DispatchLocked();
    return Slot;
}
void FWorldExecutor::QueueLocked(const std::shared_ptr<FWorkerLearner>& Slot)
{
    if (!Slot->Queued && !Slot->Executing)
    {
        Slot->Queued = true;
        Queue.push_back(Slot);
    }
}
void FWorldExecutor::Enqueue(const std::shared_ptr<FWorkerLearner>& Slot, FCommand Command)
{
    std::lock_guard Lock(Mutex);
    if (Closed || Slot->Stopping) return;
    Slot->Commands.push_back(Command);
    QueueLocked(Slot);
    DispatchLocked();
}
void FWorldExecutor::Wake(const std::shared_ptr<FWorkerLearner>& Slot)
{
    std::lock_guard Lock(Mutex);
    if (Closed || Slot->Stopping) return;
    Slot->NeedsTrain = true;
    QueueLocked(Slot);
    DispatchLocked();
}
void FWorldExecutor::Release(const std::shared_ptr<FWorkerLearner>& Slot)
{
    std::lock_guard Lock(Mutex);
    if (!Owned.Contains(Slot->Id)) return;
    Slot->Stopping = true;
    QueueLocked(Slot);
    DispatchLocked();
}
void FWorldExecutor::Close()
{
    std::lock_guard Lock(Mutex);
    Closed = true;
    for (auto& Pair : Owned)
    {
        Pair.Value->Stopping = true;
        QueueLocked(Pair.Value);
    }
    DispatchLocked();
}
void FWorldExecutor::DispatchLocked()
{
    while (Active < Limit && !Queue.empty())
    {
        auto Slot = Queue.front();
        Queue.pop_front();
        Slot->Queued = false;
        Slot->Executing = true;
        FCommand Command;
        if (Slot->Stopping) Command.Kind = EWork::Dispose;
        else if (!Slot->Commands.empty())
        {
            Command = Slot->Commands.front();
            Slot->Commands.pop_front();
        }
        else
        {
            Command.Kind = EWork::Train;
            Slot->NeedsTrain = false;
        }
        ++Active;
        Peak = FMath::Max(Peak, Active);
        auto Self = shared_from_this();
#if WITH_DEV_AUTOMATION_TESTS
        auto Gate = TestGate;
        Async(EAsyncExecution::ThreadPool, [Self, Slot, Command, Gate]
        {
            if (Gate) Gate(Command.Kind);
            Self->Execute(Slot, Command);
        });
#else
        Async(EAsyncExecution::ThreadPool, [Self, Slot, Command] { Self->Execute(Slot, Command); });
#endif
    }
}
void FWorldExecutor::Notify(const std::shared_ptr<FWorkerLearner>& Slot, const FCommand& Command, const FString& Error)
{
    // Only the posted continuation accesses UObjects. Workers own numerical state and value copies.
    auto Callback = Completion;
    const FGuid Id = Slot->Id;
    AsyncTask(ENamedThreads::GameThread, [Callback, Id, Command, Error]
    { Callback(Id, static_cast<uint8>(Command.Kind), Command.Binding, Command.Operation, Error); });
}
void FWorldExecutor::Execute(std::shared_ptr<FWorkerLearner> Slot, FCommand Command)
{
    check(!IsInGameThread());
    bool More = false;
    if (Command.Kind == EWork::Dispose)
    {
        if (Slot->Learner) Slot->Learner->Stop();
        Slot->Learner.reset(); // Waiting for training/destruction never belongs on the game thread.
    }
    else if (Command.Kind == EWork::Create)
    {
        std::string Error;
        Slot->Learner = FLearner::Create(Slot->Profile, Slot->Settings, Error);
        if (!Slot->Stopping) Notify(Slot, Command, UTF8_TO_TCHAR(Error.c_str()));
    }
    else if (!Slot->Stopping && Slot->Learner)
    {
        if (Command.Kind == EWork::Reset)
        {
            Slot->Learner->Reset(Command.Seed);
            if (!Slot->Stopping) Notify(Slot, Command, FString());
        }
        else if (Command.Kind == EWork::Save)
        {
            // File I/O and the wait for an active burst stay on this worker; inference continues.
            std::string Detail = "could not create the checkpoint directory";
            ECheckpointResult Result = ECheckpointResult::IoError;
            const FString Directory = FPaths::GetPath(FString(UTF8_TO_TCHAR(Command.Path.c_str())));
            if (Directory.IsEmpty() || IFileManager::Get().MakeDirectory(*Directory, true))
            {
                Detail.clear();
                Result = Slot->Learner->SaveCheckpoint(Command.Path, Command.Mode, Detail);
            }
            if (!Slot->Stopping)
                Notify(Slot, Command, Result == ECheckpointResult::Ok ? FString() : CheckpointError(TEXT("Save failed"), Result, Detail));
        }
        else if (Command.Kind == EWork::Load)
        {
            // Failure leaves the live learner unchanged; success replaces it like Reset.
            std::string Detail;
            const ECheckpointResult Result = Slot->Learner->LoadCheckpoint(Command.Path, Detail);
            if (!Slot->Stopping)
                Notify(Slot, Command, Result == ECheckpointResult::Ok ? FString() : CheckpointError(TEXT("Load refused"), Result, Detail));
        }
        else if (Command.Kind == EWork::Fence)
        {
            Notify(Slot, Command, FString());
            More = Slot->Learner->ReadStats().LearningEnabled;
        }
        else More = Slot->Learner->Train();
    }

    std::lock_guard Lock(Mutex);
    --Active;
    Slot->Executing = false;
    if (Command.Kind == EWork::Train) ++Bursts;
    if (Command.Kind == EWork::Dispose)
    {
        Slot->Commands.clear();
        Owned.Remove(Slot->Id);
        ++Disposed;
    }
    else
    {
        Slot->NeedsTrain = Slot->NeedsTrain || More;
        if (Slot->Stopping || !Slot->Commands.empty() || Slot->NeedsTrain) QueueLocked(Slot);
    }
    DispatchLocked();
}
FRLToolsWorldStats FWorldExecutor::Statistics() const
{
    std::lock_guard Lock(Mutex);
    FRLToolsWorldStats Stats;
    Stats.ActiveWorkers = Active;
    Stats.QueuedLearners = static_cast<int32>(Queue.size());
    Stats.WorkerLimit = Limit;
    Stats.PeakWorkers = Peak;
    Stats.CompletedBursts = Bursts;
    Stats.DisposedLearners = Disposed;
    return Stats;
}
#if WITH_DEV_AUTOMATION_TESTS
void FWorldExecutor::SetWorkGateForTests(std::function<void(EWork)> Gate)
{
    std::lock_guard Lock(Mutex);
    TestGate = std::move(Gate);
}
#endif
FWorldLearner* FWorldService::Ready(FGuid Id, FString& Error)
{
    auto* Binding = Bindings.Find(Id);
    auto* Learner = Binding ? Learners.Find(Binding->Learner) : nullptr;
    if (!Learner || Learner->Phase != EBindingPhase::Ready)
    {
        Error = TEXT("Learner unavailable: initialization/reset is pending or this binding has stopped.");
        return nullptr;
    }
    return Learner;
}
void FWorldService::Prune(FWorldBinding& Binding)
{
    const double Now = FPlatformTime::Seconds();
    for (auto It = Binding.Decisions.CreateIterator(); It; ++It)
        if (Now >= It.Value().Deadline) It.RemoveCurrent();
}
}
