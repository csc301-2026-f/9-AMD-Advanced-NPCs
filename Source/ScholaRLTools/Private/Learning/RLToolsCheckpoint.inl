// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// FLearner checkpoint operations. Included once, at the end of RLToolsLearner.cpp (needs FLearner::FImpl and MakeState).
namespace Schola::RLTools
{
const char* ToString(ECheckpointResult Result)
{
    switch (Result)
    {
    case ECheckpointResult::Ok: return "Ok";
    case ECheckpointResult::IoError: return "IoError";
    case ECheckpointResult::BadFormat: return "BadFormat";
    case ECheckpointResult::Mismatch: return "Mismatch";
    case ECheckpointResult::Unsupported: return "Unsupported";
    case ECheckpointResult::NotReady: return "NotReady";
    }
    return "Unknown";
}

ECheckpointResult FLearner::SaveCheckpoint(const std::string& Path, ECheckpointMode Mode, std::string& Error)
{
    auto& I = *Impl;
    if (Mode != ECheckpointMode::PolicyExport && Mode != ECheckpointMode::TrainingState && Mode != ECheckpointMode::FullResume)
    { Error = "Unknown checkpoint mode."; return ECheckpointResult::Unsupported; }
    if (I.Profile.Id.size() >= sizeof(Checkpoint::FHeader::ProfileId) || std::strlen(I.Profile.Revision) >= sizeof(Checkpoint::FHeader::Revision))
    { Error = "Profile identifier or revision is too long for the checkpoint header."; return ECheckpointResult::Unsupported; }
    // Waits for an active Train burst; the training state is only mutated under TrainingMutex.
    std::lock_guard TrainingLock(I.TrainingMutex);
    FStats Stats;
    {
        std::lock_guard AdmissionLock(I.AdmissionMutex);
        if (I.StopRequested || !I.Stats.Running) { Error = "Learner is stopped."; return ECheckpointResult::NotReady; }
        Stats = I.Stats;
    }
    // Serialization and file I/O run without AdmissionMutex; Decide/Submit/CapturePolicy continue meanwhile.
    std::vector<char> Payload = I.State->Serialize(Mode, Stats);
    Checkpoint::FHeader Header{};
    std::memcpy(Header.Magic, Checkpoint::Magic, sizeof(Header.Magic));
    Header.Format = Checkpoint::Format;
    Header.Mode = static_cast<std::uint32_t>(Mode);
    Header.Algorithm = static_cast<std::uint32_t>(I.Profile.Algorithm);
    Header.Observations = I.Profile.Observations;
    Header.Actions = I.Profile.Actions;
    std::memcpy(Header.ProfileId, I.Profile.Id.c_str(), I.Profile.Id.size());
    std::memcpy(Header.Revision, I.Profile.Revision, std::strlen(I.Profile.Revision));
    Header.PreprocessingFingerprint = Checkpoint::PreprocessingFingerprint(I.Settings);
    Header.Version = Stats.Version;
    Header.Updates = Mode == ECheckpointMode::PolicyExport ? 0 : Stats.Updates;
    Header.ReplaySize = Mode == ECheckpointMode::FullResume && I.Profile.Algorithm != EAlgorithm::PPO ? I.State->Occupancy() : 0;
    Header.PayloadSize = Payload.size();
    Header.PayloadChecksum = Checkpoint::Checksum(Header, Payload.data(), Payload.size());
    const auto Result = Checkpoint::WriteFile(Path, Header, Payload, Error);
    if (Result == ECheckpointResult::Ok) Error.clear();
    return Result;
}

ECheckpointResult FLearner::ReadCheckpointInfo(const std::string& Path, FCheckpointInfo& OutInfo, std::string& Error)
{
    Checkpoint::FFile File(Path, "rb");
    Checkpoint::FHeader Header;
    const auto Result = Checkpoint::ReadHeader(File, Path, Header, Error);
    if (Result != ECheckpointResult::Ok) return Result;
    Checkpoint::ToInfo(Header, OutInfo);
    Error.clear();
    return ECheckpointResult::Ok;
}

ECheckpointResult FLearner::LoadCheckpoint(const std::string& Path, std::string& Error, FCheckpointInfo* OutInfo)
{
    auto& I = *Impl;
    // Like Reset: no Train burst runs while the replacement is prepared, so I.State/I.Settings are stable.
    std::lock_guard TrainingLock(I.TrainingMutex);
    {
        std::lock_guard AdmissionLock(I.AdmissionMutex);
        if (I.StopRequested || !I.Stats.Running) { Error = "Learner is stopped."; return ECheckpointResult::NotReady; }
    }
    Checkpoint::FHeader Header;
    std::vector<char> Payload;
    Checkpoint::FFile File(Path, "rb");
    {
        const auto Result = Checkpoint::ReadHeader(File, Path, Header, Error);
        if (Result != ECheckpointResult::Ok) return Result;
    }
    const auto Mode = static_cast<ECheckpointMode>(Header.Mode);
    if (I.Profile.Id != Header.ProfileId || static_cast<std::uint32_t>(I.Profile.Algorithm) != Header.Algorithm
        || I.Profile.Observations != Header.Observations || I.Profile.Actions != Header.Actions)
    { Error = std::string("Checkpoint was written for profile ") + Header.ProfileId + ", not " + I.Profile.Id + "."; return ECheckpointResult::Mismatch; }
    if (std::strcmp(I.Profile.Revision, Header.Revision) != 0)
    { Error = std::string("Checkpoint was written with RLtools revision ") + Header.Revision + "."; return ECheckpointResult::Mismatch; }
    if (Checkpoint::PreprocessingFingerprint(I.Settings) != Header.PreprocessingFingerprint)
    { Error = "Checkpoint observation preprocessing differs from this learner's ObservationMean/ObservationStd."; return ECheckpointResult::Mismatch; }

    // Construct and fill the replacement while the live learner keeps admitting and deciding.
    FSettings Settings = I.Settings;
    auto Replacement = MakeState(I.Profile, Settings);
    if (!Replacement) { Error = "No compiled state for this profile."; return ECheckpointResult::Unsupported; }
    // This build's own serialization fixes the exact payload size, so nothing larger is ever allocated or read.
    const std::vector<char> Reference = Replacement->Serialize(Mode, FStats{});
    if (Header.PayloadSize != Reference.size())
    { Error = "Checkpoint tensor structure differs from this build's networks/optimizers."; return ECheckpointResult::Mismatch; }
    Payload.resize(Reference.size());
    if (std::fread(Payload.data(), 1, Payload.size(), File.Handle) != Payload.size())
    { Error = "Cannot read checkpoint payload."; return ECheckpointResult::IoError; }
    File.Close();
    if (Checkpoint::Checksum(Header, Payload.data(), Payload.size()) != Header.PayloadChecksum)
    { Error = "Checkpoint checksum mismatch (corrupt file)."; return ECheckpointResult::BadFormat; }
    if (!Checkpoint::SameStructure(Reference, Payload.data(), Payload.size()))
    { Error = "Checkpoint tensor structure differs from this build's networks/optimizers."; return ECheckpointResult::Mismatch; }
    FStats Restored;
    if (!Replacement->Deserialize(Mode, Payload.data(), Payload.size(), Restored))
    { Error = "Checkpoint contents are inconsistent."; return ECheckpointResult::BadFormat; }
    Payload = {};
    auto Policy = Replacement->Snapshot();
    Policy->Profile = I.Profile;
    Policy->Settings = Settings;
    {
        // Reject restored weights that produce non-finite actions (or PPO values) for a zero observation.
        std::vector<float> Input, Action;
        float LogProbability = 0, Value = 0;
        if (!Policy->Preprocess(std::vector<float>(I.Profile.Observations, 0.0f), Input)
            || !Policy->Sample(Input, true, 0, Action, LogProbability, Value) || !Finite(Action, I.Profile.Actions)
            || !std::isfinite(Value))
        { Error = "Checkpoint actor produces non-finite output."; return ECheckpointResult::BadFormat; }
    }
    std::uint64_t Generation, Version;
    {
        std::lock_guard AdmissionLock(I.AdmissionMutex);
        Generation = I.Stats.Generation + 1;
        Version = std::max<std::uint64_t>(I.Stats.Version + 1, Header.Version);
    }
    Policy->Generation = Generation;
    Policy->Version = Version;
    Policy->Owner = I.Owner;
    const bool Training = Mode != ECheckpointMode::PolicyExport;
    const bool Replay = Mode == ECheckpointMode::FullResume && I.Profile.Algorithm != EAlgorithm::PPO;
    std::deque<FExperience> RetiredRecords;
    std::unique_ptr<FAlgorithmState> RetiredState;
    std::shared_ptr<const FPolicy> RetiredPolicy;
    {
        std::lock_guard AdmissionLock(I.AdmissionMutex);
        if (I.StopRequested || !I.Stats.Running) { Error = "Learner stopped during load."; return ECheckpointResult::NotReady; }
        const bool LearningEnabled = I.Stats.LearningEnabled;
        RetiredRecords.swap(I.Queue);
        RetiredState = std::move(I.State);
        I.State = std::move(Replacement);
        I.Stats = {};
        I.Stats.Generation = Generation;
        I.Stats.Version = Version;
        I.Stats.LearningEnabled = LearningEnabled; // Loading never unfreezes a frozen learner.
        if (Training)
        {
            I.Stats.Updates = Header.Updates;
            I.Stats.ActorUpdates = Restored.ActorUpdates;
            I.Stats.CriticUpdates = Restored.CriticUpdates;
            I.Stats.TargetUpdates = Restored.TargetUpdates;
        }
        if (Replay)
        {
            // Restored transitions count toward LearningStarts, so training resumes without a new warm-up.
            I.Stats.ReplaySize = I.State->Occupancy();
            I.Stats.Stored = Restored.Stored; // all records ever stored, not capped at replay capacity
        }
        I.RecordsSinceTrain = I.UpdatesSincePublish = 0;
        I.PPOUpdating = false;
        I.SkipInitialRandom = true;
        I.AgentSequences.clear();
        RetiredPolicy = I.ExchangePublished(std::move(Policy));
    }
    // Reclaim the old training state, queued records and snapshot outside the admission lock.
    if (OutInfo) Checkpoint::ToInfo(Header, *OutInfo);
    Error.clear();
    return ECheckpointResult::Ok;
}
}
