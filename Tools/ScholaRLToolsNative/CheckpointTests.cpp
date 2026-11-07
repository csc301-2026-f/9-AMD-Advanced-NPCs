// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Engine-free checkpoint save/load tests for Schola::RLTools::FLearner.
// Lives outside Source/ on purpose: UnrealBuildTool compiles every .cpp under Source and a main() there
// would break the Unreal build. Built and run by build.sh in this directory.
#include "RLToolsLearner.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace RL = Schola::RLTools;
namespace fs = std::filesystem;

namespace
{
int Passed = 0, Failed = 0, Skipped = 0;
std::vector<std::string> Failures;
std::string Root;

void Report(bool Ok, const std::string& Profile, const std::string& Check, const std::string& Detail)
{
    std::printf("%s %s %s: %s\n", Ok ? "PASS" : "FAIL", Profile.c_str(), Check.c_str(), Detail.c_str());
    std::fflush(stdout);
    if (Ok) ++Passed;
    else { ++Failed; Failures.push_back(Profile + " " + Check + ": " + Detail); }
}
void Skip(const std::string& Profile, const std::string& Check, const std::string& Detail)
{
    std::printf("SKIP %s %s: %s\n", Profile.c_str(), Check.c_str(), Detail.c_str());
    ++Skipped;
}

const char* ModeName(RL::ECheckpointMode Mode)
{
    switch (Mode)
    {
    case RL::ECheckpointMode::PolicyExport: return "PolicyExport";
    case RL::ECheckpointMode::TrainingState: return "TrainingState";
    case RL::ECheckpointMode::FullResume: return "FullResume";
    }
    return "?";
}
std::string Res(RL::ECheckpointResult R)
{
    const char* S = RL::ToString(R);
    return S ? S : "(null)";
}
std::string Num(std::uint64_t V) { return std::to_string(V); }

const RL::ECheckpointMode Modes[] = { RL::ECheckpointMode::PolicyExport, RL::ECheckpointMode::TrainingState, RL::ECheckpointMode::FullResume };

RL::FSettings MakeSettings(const RL::FProfile& P, unsigned Seed)
{
    RL::FSettings S;
    S.Seed = Seed;
    S.PublishEveryUpdates = 1;
    S.LearningStarts = P.BatchSize;
    return S;
}
std::unique_ptr<RL::FLearner> Make(const RL::FProfile& P, const RL::FSettings& S)
{
    std::string Error;
    auto L = RL::FLearner::Create(P.Id, S, Error);
    if (!L) Report(false, P.Id, "create", Error);
    return L;
}

float Value(unsigned Index, unsigned Dim)
{
    const unsigned H = (Index * 2654435761u) ^ (Dim * 40503u + 0x9E3779B9u);
    return static_cast<float>((H >> 8) % 2001u) / 1000.0f - 1.0f;
}
std::vector<float> Obs(const RL::FProfile& P, unsigned Index)
{
    std::vector<float> O(P.Observations);
    for (unsigned D = 0; D < P.Observations; ++D) O[D] = Value(Index, D);
    return O;
}

/** Deterministic outputs on 20 fixed observations, concatenated. */
bool Outputs(const RL::FLearner& L, const RL::FProfile& P, std::vector<float>& Out)
{
    Out.clear();
    const auto Policy = L.CapturePolicy();
    if (!Policy) return false;
    for (unsigned K = 0; K < 20; ++K)
    {
        std::vector<float> A;
        if (!Policy->Evaluate(Obs(P, 100000 + K), A) || A.size() != P.Actions) return false;
        Out.insert(Out.end(), A.begin(), A.end());
    }
    return true;
}
bool BitEqual(const std::vector<float>& A, const std::vector<float>& B)
{
    return A.size() == B.size() && (A.empty() || std::memcmp(A.data(), B.data(), A.size() * sizeof(float)) == 0);
}
bool AllFinite(const std::vector<float>& A)
{
    return std::all_of(A.begin(), A.end(), [](float V) { return std::isfinite(V); });
}

struct FFeed { unsigned Step = 0; std::uint64_t Episode = 0; };

/** One Decide -> Submit step on a varying observation. Returns the submit result (or NotReady if Decide failed). */
RL::ESubmitResult Step(RL::FLearner& L, const RL::FProfile& P, FFeed& F)
{
    const auto O = Obs(P, F.Step);
    RL::FDecision D;
    if (!L.Decide(O, 1, F.Episode, false, D)) return RL::ESubmitResult::NotReady;
    ++F.Step;
    const auto N = Obs(P, F.Step);
    const bool Terminated = F.Step % 16 == 0;
    float Reward = 0.1f * O[0];
    for (float A : D.GetAction()) Reward -= A * A;
    const auto R = L.Submit(D, D.GetAction(), Reward, N, Terminated, false);
    if (Terminated) ++F.Episode;
    return R;
}
void Drain(RL::FLearner& L)
{
    for (unsigned B = 0; B < 1000 && L.Train(); ++B) {}
}
/** Decide -> Submit -> Train until the published Version reaches Target. */
bool TrainUntil(RL::FLearner& L, const RL::FProfile& P, std::uint64_t Target, FFeed& F, unsigned MaxSteps = 20000)
{
    for (unsigned I = 0; I < MaxSteps; ++I)
    {
        if (L.ReadStats().Version >= Target) return true;
        Step(L, P, F);
        Drain(L);
    }
    return L.ReadStats().Version >= Target;
}

std::string PathFor(const RL::FProfile& P, const std::string& Name)
{
    std::string Id = P.Id;
    std::replace(Id.begin(), Id.end(), '/', '_');
    return Root + "/" + Id + "-" + Name + ".ckpt";
}
bool Exists(const std::string& Path)
{
    std::error_code Ec;
    return fs::exists(Path, Ec);
}
void Remove(const std::string& Path)
{
    std::error_code Ec;
    fs::remove(Path, Ec);
}
bool ReadBytes(const std::string& Path, std::vector<unsigned char>& Out)
{
    std::ifstream F(Path, std::ios::binary);
    if (!F) return false;
    Out.assign(std::istreambuf_iterator<char>(F), std::istreambuf_iterator<char>());
    return true;
}
bool WriteBytes(const std::string& Path, const std::vector<unsigned char>& Bytes)
{
    std::ofstream F(Path, std::ios::binary | std::ios::trunc);
    if (!F) return false;
    F.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
    return static_cast<bool>(F);
}

/** Snapshot of the observable live state used to prove that a refusal changed nothing. */
struct FLive
{
    std::vector<float> Out;
    std::uint64_t Generation = 0, Version = 0, Updates = 0;
    unsigned ReplaySize = 0;
};
FLive Capture(RL::FLearner& L, const RL::FProfile& P)
{
    FLive S;
    Outputs(L, P, S.Out);
    const auto Stats = L.ReadStats();
    S.Generation = Stats.Generation; S.Version = Stats.Version; S.Updates = Stats.Updates; S.ReplaySize = Stats.ReplaySize;
    return S;
}

/** Load Path into L, expect Expected, and verify the live learner is unchanged. */
void ExpectRefusal(RL::FLearner& L, const RL::FProfile& P, const std::string& Check, const std::string& Path,
    RL::ECheckpointResult Expected, const char* Clause)
{
    Drain(L);
    const FLive Before = Capture(L, P);
    std::string Error;
    RL::FCheckpointInfo Info;
    const auto R = L.LoadCheckpoint(Path, Error, &Info);
    const FLive After = Capture(L, P);
    const bool Unchanged = BitEqual(Before.Out, After.Out) && Before.Generation == After.Generation
        && Before.Version == After.Version && Before.Updates == After.Updates && Before.ReplaySize == After.ReplaySize;
    RL::FDecision D;
    const bool StillDecides = L.Decide(Obs(P, 7), 2, 0, false, D);
    Report(R == Expected && Unchanged && StillDecides, P.Id, "refuse/" + Check,
        "result=" + Res(R) + " expected=" + Res(Expected) + (Unchanged ? " live-unchanged" : " LIVE-CHANGED(gen " + Num(Before.Generation)
        + "->" + Num(After.Generation) + ", ver " + Num(Before.Version) + "->" + Num(After.Version) + ")")
        + (StillDecides ? "" : " DECIDE-FAILED") + " [" + Clause + "] err=\"" + Error + "\"");
}

/** Find the PayloadSize field in the header region and overwrite it (8-byte LE first, then 4-byte LE). */
bool PatchPayloadSize(std::vector<unsigned char> Bytes, std::uint64_t PayloadSize, std::uint64_t NewSize,
    const std::string& OutPath, std::string& Where)
{
    const std::size_t Header = Bytes.size() > PayloadSize ? Bytes.size() - PayloadSize : Bytes.size();
    for (unsigned Width : { 8u, 4u })
    {
        if (Width == 4 && (PayloadSize > 0xFFFFFFFFull)) continue;
        for (std::size_t Offset = 0; Offset + Width <= Header; ++Offset)
        {
            std::uint64_t V = 0;
            for (unsigned B = 0; B < Width; ++B) V |= static_cast<std::uint64_t>(Bytes[Offset + B]) << (8 * B);
            if (V != PayloadSize) continue;
            const std::uint64_t Write = Width == 4 ? std::min<std::uint64_t>(NewSize, 0xFFFFFFFFull) : NewSize;
            for (unsigned B = 0; B < Width; ++B) Bytes[Offset + B] = static_cast<unsigned char>(Write >> (8 * B));
            Where = "offset " + std::to_string(Offset) + " width " + std::to_string(Width);
            return WriteBytes(OutPath, Bytes);
        }
    }
    return false;
}

struct FSaved
{
    std::string Path;
    RL::FStats Stats;
    std::vector<float> Out;
};

void RunProfile(const RL::FProfile& P, const std::vector<RL::FProfile>& All)
{
    const bool OffPolicy = P.Algorithm != RL::EAlgorithm::PPO;
    auto Saver = Make(P, MakeSettings(P, 11));
    if (!Saver) return;
    FFeed Feed;
    if (!TrainUntil(*Saver, P, 2, Feed))
    {
        Report(false, P.Id, "setup", "saver never reached version 2, version=" + Num(Saver->ReadStats().Version));
        return;
    }
    Drain(*Saver);

    // Save all three modes from one identical state.
    FSaved Saved[3];
    for (int M = 0; M < 3; ++M)
    {
        const auto Mode = Modes[M];
        FSaved& S = Saved[M];
        S.Path = PathFor(P, ModeName(Mode));
        Remove(S.Path);
        S.Stats = Saver->ReadStats();
        Outputs(*Saver, P, S.Out);
        std::string Error;
        const auto R = Saver->SaveCheckpoint(S.Path, Mode, Error);
        Report(R == RL::ECheckpointResult::Ok && Exists(S.Path), P.Id, std::string("save/") + ModeName(Mode),
            "result=" + Res(R) + " version=" + Num(S.Stats.Version) + " replay=" + Num(S.Stats.ReplaySize) + " err=\"" + Error + "\"");
        // 8. Atomic write leaves no temporary behind.
        Report(!Exists(S.Path + ".tmp"), P.Id, std::string("atomic/") + ModeName(Mode), "no <path>.tmp after save");
        const auto After = Saver->ReadStats();
        Report(After.Version == S.Stats.Version && After.Generation == S.Stats.Generation, P.Id,
            std::string("save-nonmutating/") + ModeName(Mode), "save does not change version/generation");
    }

    // 7. ReadCheckpointInfo: header only, no learner involved.
    RL::FCheckpointInfo Infos[3];
    for (int M = 0; M < 3; ++M)
    {
        const auto Mode = Modes[M];
        RL::FCheckpointInfo& I = Infos[M];
        std::string Error;
        const auto R = RL::FLearner::ReadCheckpointInfo(Saved[M].Path, I, Error);
        std::vector<unsigned char> Bytes;
        ReadBytes(Saved[M].Path, Bytes);
        const bool ModeOk = I.Mode == Mode
            || (!OffPolicy && Mode == RL::ECheckpointMode::FullResume && I.Mode == RL::ECheckpointMode::TrainingState);
        const bool ReplayOk = !(OffPolicy && Mode == RL::ECheckpointMode::FullResume) || I.ReplaySize == Saved[M].Stats.ReplaySize;
        const bool Ok = R == RL::ECheckpointResult::Ok && I.Format != 0 && ModeOk && I.ProfileId == P.Id
            && I.Revision == P.Revision && I.Algorithm == P.Algorithm && I.Observations == P.Observations
            && I.Actions == P.Actions && I.Version == Saved[M].Stats.Version && ReplayOk
            && I.PayloadSize > 0 && I.PayloadSize < Bytes.size();
        Report(Ok, P.Id, std::string("info/") + ModeName(Mode),
            "result=" + Res(R) + " format=" + Num(I.Format) + " mode=" + ModeName(I.Mode) + " profile=" + I.ProfileId
            + " dims=" + Num(I.Observations) + "x" + Num(I.Actions) + " version=" + Num(I.Version) + "/" + Num(Saved[M].Stats.Version)
            + " updates=" + Num(I.Updates) + " replay=" + Num(I.ReplaySize) + "/" + Num(Saved[M].Stats.ReplaySize)
            + " payload=" + Num(I.PayloadSize) + " file=" + Num(Bytes.size()) + " err=\"" + Error + "\"");
    }
    Report(Infos[0].PayloadSize < Infos[1].PayloadSize, P.Id, "info/payload-order",
        "PolicyExport " + Num(Infos[0].PayloadSize) + " < TrainingState " + Num(Infos[1].PayloadSize));
    if (OffPolicy)
        Report(Infos[2].PayloadSize > Infos[1].PayloadSize, P.Id, "info/payload-order-replay",
            "FullResume " + Num(Infos[2].PayloadSize) + " > TrainingState " + Num(Infos[1].PayloadSize) + " (replay included)");
    {
        RL::FCheckpointInfo I;
        std::string Error;
        const auto R = RL::FLearner::ReadCheckpointInfo(Root + "/does-not-exist.ckpt", I, Error);
        Report(R == RL::ECheckpointResult::IoError, P.Id, "info/missing", "result=" + Res(R) + " expected=IoError");
    }

    // 1-4. Load into a fresh learner (same profile/settings, different seed).
    for (int M = 0; M < 3; ++M)
    {
        const auto Mode = Modes[M];
        const std::string Name = ModeName(Mode);
        auto Fresh = Make(P, MakeSettings(P, 99 + M));
        if (!Fresh) continue;
        const auto Before = Fresh->ReadStats();
        std::string Error;
        RL::FCheckpointInfo Info;
        const auto R = Fresh->LoadCheckpoint(Saved[M].Path, Error, &Info);
        if (R != RL::ECheckpointResult::Ok)
        {
            Report(false, P.Id, "load/" + Name, "result=" + Res(R) + " err=\"" + Error + "\"");
            continue;
        }
        Report(Info.ProfileId == Infos[M].ProfileId && Info.Mode == Infos[M].Mode && Info.Version == Infos[M].Version
            && Info.PayloadSize == Infos[M].PayloadSize, P.Id, "load-info/" + Name, "OutInfo matches ReadCheckpointInfo");
        std::vector<float> Out;
        const bool Evaluated = Outputs(*Fresh, P, Out);
        // 1. Bit-identical deterministic policy.
        Report(Evaluated && BitEqual(Out, Saved[M].Out), P.Id, "roundtrip/" + Name,
            Evaluated ? (BitEqual(Out, Saved[M].Out) ? "20 deterministic evaluations bit-identical" : "outputs differ from saver") : "Evaluate failed");
        const auto After = Fresh->ReadStats();
        // 4. Version/Generation on load.
        const std::uint64_t ExpectVersion = std::max<std::uint64_t>(Before.Version + 1, Saved[M].Stats.Version);
        Report(After.Version == ExpectVersion && After.Generation == Before.Generation + 1
            && Fresh->CapturePolicy()->GetVersion() == After.Version && Fresh->CapturePolicy()->GetGeneration() == After.Generation,
            P.Id, "version/fresh/" + Name, "version " + Num(Before.Version) + "->" + Num(After.Version) + " expected " + Num(ExpectVersion)
            + ", generation " + Num(Before.Generation) + "->" + Num(After.Generation) + ", snapshot v" + Num(Fresh->CapturePolicy()->GetVersion())
            + " g" + Num(Fresh->CapturePolicy()->GetGeneration()));
        // 3. What state came back.
        if (Mode == RL::ECheckpointMode::PolicyExport)
            Report(After.Updates == 0 && After.ReplaySize == 0 && After.RolloutSize == 0, P.Id, "stats/" + Name,
                "updates=" + Num(After.Updates) + " replay=" + Num(After.ReplaySize) + " (expected 0/0: fresh critics, empty replay)");
        else if (Mode == RL::ECheckpointMode::TrainingState || !OffPolicy)
            Report(After.ReplaySize == 0 && After.RolloutSize == 0 && After.Updates == Saved[M].Stats.Updates, P.Id, "stats/" + Name,
                "replay=" + Num(After.ReplaySize) + " rollout=" + Num(After.RolloutSize) + " (expected 0), updates=" + Num(After.Updates)
                + " saver=" + Num(Saved[M].Stats.Updates));
        else
            Report(After.ReplaySize == Saved[M].Stats.ReplaySize && After.Updates == Saved[M].Stats.Updates, P.Id, "stats/" + Name,
                "replay=" + Num(After.ReplaySize) + " saver=" + Num(Saved[M].Stats.ReplaySize) + ", updates=" + Num(After.Updates)
                + " saver=" + Num(Saved[M].Stats.Updates));
        // 2. The loaded learner accepts experience, trains and publishes a higher version.
        FFeed F2;
        F2.Step = 5000;
        const RL::ESubmitResult First = Step(*Fresh, P, F2);
        const bool Trained = TrainUntil(*Fresh, P, After.Version + 1, F2);
        const auto End = Fresh->ReadStats();
        Report(First == RL::ESubmitResult::Accepted && Trained && End.Generation == After.Generation, P.Id, "continue/" + Name,
            std::string("first submit ") + (First == RL::ESubmitResult::Accepted ? "Accepted" : "not accepted")
            + ", version " + Num(After.Version) + "->" + Num(End.Version) + ", updates " + Num(After.Updates) + "->" + Num(End.Updates));
        Fresh->Stop();
        Error.clear();
        const auto Stopped = Fresh->LoadCheckpoint(Saved[M].Path, Error);
        Report(Stopped == RL::ECheckpointResult::NotReady, P.Id, "stopped/" + Name, "load into stopped learner result=" + Res(Stopped) + " expected=NotReady");
    }

    // 4/5. Load into the saver itself after it trained further; outstanding decisions are retired.
    {
        const auto Target = Saver->ReadStats().Version + 2;
        TrainUntil(*Saver, P, Target, Feed);
        Drain(*Saver);
        const auto Before = Saver->ReadStats();
        RL::FDecision Old;
        const bool Decided = Saver->Decide(Obs(P, 3), 1, 0, false, Old);
        std::string Error;
        const auto R = Saver->LoadCheckpoint(Saved[1].Path, Error);
        const auto After = Saver->ReadStats();
        const std::uint64_t ExpectVersion = std::max<std::uint64_t>(Before.Version + 1, Saved[1].Stats.Version);
        Report(R == RL::ECheckpointResult::Ok && After.Version == ExpectVersion && After.Version > Before.Version
            && After.Generation == Before.Generation + 1, P.Id, "version/self",
            "result=" + Res(R) + " version " + Num(Before.Version) + "->" + Num(After.Version) + " expected max(" + Num(Before.Version)
            + "+1," + Num(Saved[1].Stats.Version) + ")=" + Num(ExpectVersion) + ", generation " + Num(Before.Generation) + "->" + Num(After.Generation));
        std::vector<float> Out;
        Outputs(*Saver, P, Out);
        Report(BitEqual(Out, Saved[1].Out), P.Id, "roundtrip/self", "self-load restores saved outputs bit-identically");
        if (Decided)
        {
            const auto S = Saver->Submit(Old, Old.GetAction(), 0, Obs(P, 4), false, false);
            Report(S == RL::ESubmitResult::Rejected || S == RL::ESubmitResult::NotReady, P.Id, "retire/old-decision",
                std::string("submit of pre-load decision -> ") + (S == RL::ESubmitResult::Rejected ? "Rejected" : S == RL::ESubmitResult::NotReady ? "NotReady"
                : S == RL::ESubmitResult::Accepted ? "Accepted" : "other"));
        }
        else Report(false, P.Id, "retire/old-decision", "could not Decide before load");
        RL::FDecision New;
        const bool NewDecided = Saver->Decide(Obs(P, 3), 1, 0, false, New);
        const auto NewSubmit = NewDecided ? Saver->Submit(New, New.GetAction(), 0, Obs(P, 4), false, false) : RL::ESubmitResult::NotReady;
        Report(NewDecided && New.GetGeneration() == After.Generation && NewSubmit == RL::ESubmitResult::Accepted, P.Id, "retire/new-decision",
            "new Decide works (generation " + Num(New ? New.GetGeneration() : 0) + ") and is Accepted");
        Drain(*Saver);
    }

    // 6. Refusals leave the live learner unchanged.
    {
        RL::FLearner& Live = *Saver;
        const std::string Src = Saved[1].Path;
        std::vector<unsigned char> Bytes;
        ReadBytes(Src, Bytes);
        const std::uint64_t Payload = Infos[1].PayloadSize;
        const std::size_t Header = Bytes.size() > Payload ? Bytes.size() - Payload : 0;
        const char* Fmt = "BadFormat: not a checkpoint/truncated/corrupt";

        ExpectRefusal(Live, P, "missing", Root + "/missing-" + std::to_string(P.Observations) + ".ckpt", RL::ECheckpointResult::IoError, "IoError: missing file");
        const std::string Empty = PathFor(P, "empty");
        WriteBytes(Empty, {});
        ExpectRefusal(Live, P, "empty", Empty, RL::ECheckpointResult::BadFormat, Fmt);
        if (Header > 0)
        {
            const std::string HeaderOnly = PathFor(P, "header-only");
            WriteBytes(HeaderOnly, std::vector<unsigned char>(Bytes.begin(), Bytes.begin() + Header));
            ExpectRefusal(Live, P, "header-only", HeaderOnly, RL::ECheckpointResult::BadFormat, Fmt);
            const std::string Truncated = PathFor(P, "truncated");
            WriteBytes(Truncated, std::vector<unsigned char>(Bytes.begin(), Bytes.begin() + Header + Payload / 2));
            ExpectRefusal(Live, P, "truncated-payload", Truncated, RL::ECheckpointResult::BadFormat, Fmt);
            const std::string OneShort = PathFor(P, "one-short");
            WriteBytes(OneShort, std::vector<unsigned char>(Bytes.begin(), Bytes.end() - 1));
            ExpectRefusal(Live, P, "truncated-1-byte", OneShort, RL::ECheckpointResult::BadFormat, Fmt);
            for (double Where : { 0.0, 0.5, 0.999 })
            {
                auto Flipped = Bytes;
                const std::size_t At = Header + static_cast<std::size_t>(static_cast<double>(Payload - 1) * Where);
                Flipped[At] ^= 0x01;
                const std::string Path = PathFor(P, "flipped");
                WriteBytes(Path, Flipped);
                ExpectRefusal(Live, P, "flipped-payload-byte@" + std::to_string(At), Path, RL::ECheckpointResult::BadFormat, "BadFormat: wrong checksum");
            }
        }
        else Report(false, P.Id, "refuse/layout", "file size " + Num(Bytes.size()) + " not larger than PayloadSize " + Num(Payload));
        {
            auto Magic = Bytes;
            if (!Magic.empty()) Magic[0] ^= 0xFF;
            const std::string Path = PathFor(P, "magic");
            WriteBytes(Path, Magic);
            ExpectRefusal(Live, P, "wrong-magic", Path, RL::ECheckpointResult::BadFormat, Fmt);
            RL::FCheckpointInfo I;
            std::string Error;
            const auto R = RL::FLearner::ReadCheckpointInfo(Path, I, Error);
            Report(R == RL::ECheckpointResult::BadFormat, P.Id, "info/wrong-magic", "result=" + Res(R) + " expected=BadFormat");
        }
        {
            std::vector<unsigned char> Garbage(4096);
            for (std::size_t K = 0; K < Garbage.size(); ++K) Garbage[K] = static_cast<unsigned char>(Value(static_cast<unsigned>(K), 1) * 127 + 128);
            const std::string Path = PathFor(P, "garbage");
            WriteBytes(Path, Garbage);
            ExpectRefusal(Live, P, "garbage", Path, RL::ECheckpointResult::BadFormat, Fmt);
        }
        for (std::uint64_t NewSize : { static_cast<std::uint64_t>(Payload + 4096), static_cast<std::uint64_t>(1) << 60 })
        {
            const std::string Path = PathFor(P, "oversize");
            std::string Where;
            const std::string Name = NewSize > (1ull << 40) ? "payload-size-2^60" : "payload-size>file";
            if (PatchPayloadSize(Bytes, Payload, NewSize, Path, Where))
            {
                std::printf("  (patched PayloadSize at %s)\n", Where.c_str());
                ExpectRefusal(Live, P, Name, Path, RL::ECheckpointResult::BadFormat, "BadFormat: sizes validated before allocating payload");
                RL::FCheckpointInfo I;
                std::string Error;
                const auto R = RL::FLearner::ReadCheckpointInfo(Path, I, Error);
                Report(R == RL::ECheckpointResult::BadFormat, P.Id, "info/" + Name, "ReadCheckpointInfo validates file size: result=" + Res(R));
            }
            else Skip(P.Id, "refuse/" + Name, "PayloadSize value not found in header bytes");
        }
        // Different profile, same algorithm; different algorithm, same shape.
        for (const auto& Other : All)
        {
            const bool SameAlgoOtherShape = Other.Algorithm == P.Algorithm && Other.Id != P.Id;
            const bool OtherAlgoSameShape = Other.Algorithm != P.Algorithm && Other.Observations == P.Observations && Other.Actions == P.Actions;
            if (!SameAlgoOtherShape && !OtherAlgoSameShape) continue;
            auto Donor = Make(Other, MakeSettings(Other, 5));
            if (!Donor) continue;
            FFeed DF;
            TrainUntil(*Donor, Other, 1, DF);
            Drain(*Donor);
            for (auto Mode : Modes)
            {
                const std::string Path = PathFor(P, std::string("donor-") + ModeName(Mode));
                std::string Error;
                if (Donor->SaveCheckpoint(Path, Mode, Error) != RL::ECheckpointResult::Ok)
                { Report(false, Other.Id, "save/donor", Error); continue; }
                ExpectRefusal(Live, P, "from-" + Other.Id + "/" + ModeName(Mode), Path, RL::ECheckpointResult::Mismatch, "Mismatch: different profile/algorithm/dimensions");
            }
        }
        // Different preprocessing.
        {
            RL::FSettings Pre = MakeSettings(P, 11);
            Pre.ObservationMean.assign(P.Observations, 0.1f);
            Pre.ObservationStd.assign(P.Observations, 2.0f);
            RL::FSettings Pre2 = Pre;
            Pre2.ObservationStd.assign(P.Observations, 3.0f);
            auto PreLearner = Make(P, Pre);
            auto PreLearner2 = Make(P, Pre2);
            if (PreLearner && PreLearner2)
            {
                FFeed PF;
                TrainUntil(*PreLearner, P, 1, PF);
                Drain(*PreLearner);
                for (auto Mode : { RL::ECheckpointMode::TrainingState, RL::ECheckpointMode::FullResume })
                {
                    const std::string Path = PathFor(P, std::string("preprocessed-") + ModeName(Mode));
                    std::string Error;
                    const auto R = PreLearner->SaveCheckpoint(Path, Mode, Error);
                    RL::FCheckpointInfo I;
                    RL::FLearner::ReadCheckpointInfo(Path, I, Error);
                    Report(R == RL::ECheckpointResult::Ok && I.PreprocessingIncluded, P.Id, std::string("info/preprocessing/") + ModeName(Mode),
                        "save=" + Res(R) + " PreprocessingIncluded=" + (I.PreprocessingIncluded ? "true" : "false"));
                    ExpectRefusal(Live, P, std::string("preprocessed-into-identity/") + ModeName(Mode), Path, RL::ECheckpointResult::Mismatch, "Mismatch: preprocessing");
                    ExpectRefusal(*PreLearner2, P, std::string("preprocessing-values-differ/") + ModeName(Mode), Path, RL::ECheckpointResult::Mismatch, "Mismatch: preprocessing");
                    ExpectRefusal(*PreLearner, P, std::string("identity-into-preprocessed/") + ModeName(Mode), Saved[Mode == RL::ECheckpointMode::FullResume ? 2 : 1].Path,
                        RL::ECheckpointResult::Mismatch, "Mismatch: preprocessing");
                }
                // Matching preprocessing loads fine.
                std::string Error;
                auto Same = Make(P, Pre);
                const auto R = Same ? Same->LoadCheckpoint(PathFor(P, "preprocessed-TrainingState"), Error) : RL::ECheckpointResult::NotReady;
                std::vector<float> A, B;
                Outputs(*PreLearner, P, A);
                if (Same) Outputs(*Same, P, B);
                Report(R == RL::ECheckpointResult::Ok && BitEqual(A, B), P.Id, "roundtrip/preprocessed", "result=" + Res(R) + " outputs "
                    + (BitEqual(A, B) ? "bit-identical" : "differ") + " err=\"" + Error + "\"");
            }
        }
        // Still trains after all refusals.
        FFeed F3;
        F3.Step = 9000;
        const auto Before = Live.ReadStats();
        Report(TrainUntil(Live, P, Before.Version + 1, F3) && Live.ReadStats().Generation == Before.Generation, P.Id, "refuse/still-trains",
            "version " + Num(Before.Version) + "->" + Num(Live.ReadStats().Version) + " after all refusals");
    }

    // 8. Unwritable target: IoError and no partial file.
    {
        const std::string Path = Root + "/no-such-dir/sub/x.ckpt";
        std::string Error;
        const auto R = Saver->SaveCheckpoint(Path, RL::ECheckpointMode::TrainingState, Error);
        Report(R == RL::ECheckpointResult::IoError && !Exists(Path) && !Exists(Path + ".tmp"), P.Id, "atomic/unwritable",
            "result=" + Res(R) + " expected=IoError, no file, no .tmp; err=\"" + Error + "\"");
        // Saving over an existing checkpoint replaces it with a valid one.
        const std::string Over = PathFor(P, "overwrite");
        WriteBytes(Over, std::vector<unsigned char>(17, 0xAB));
        const auto R2 = Saver->SaveCheckpoint(Over, RL::ECheckpointMode::TrainingState, Error);
        RL::FCheckpointInfo I;
        const auto R3 = RL::FLearner::ReadCheckpointInfo(Over, I, Error);
        Report(R2 == RL::ECheckpointResult::Ok && R3 == RL::ECheckpointResult::Ok && !Exists(Over + ".tmp"), P.Id, "atomic/overwrite",
            "save over existing file=" + Res(R2) + ", info=" + Res(R3));
    }

    // 9. Concurrency smoke: Train loop + Decide/CapturePolicy thread while the main thread saves/loads.
    {
        auto L = Make(P, MakeSettings(P, 21));
        if (!L) return;
        FFeed CF;
        TrainUntil(*L, P, 1, CF);
        std::atomic<bool> Done{false};
        std::atomic<unsigned> NonFinite{0}, Evaluations{0}, Bursts{0};
        std::thread Trainer([&] { while (!Done) { L->Train(); ++Bursts; std::this_thread::yield(); } });
        std::thread Decider([&]
        {
            FFeed DF;
            DF.Step = 20000;
            std::vector<float> Out;
            while (!Done)
            {
                Step(*L, P, DF);
                if (Outputs(*L, P, Out)) { ++Evaluations; if (!AllFinite(Out)) ++NonFinite; }
            }
        });
        unsigned Saves = 0, Loads = 0;
        std::string Errors;
        for (int K = 0; K < 5; ++K)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            const auto Mode = K % 2 ? RL::ECheckpointMode::FullResume : RL::ECheckpointMode::TrainingState;
            const std::string Path = PathFor(P, "concurrent");
            std::string Error;
            if (L->SaveCheckpoint(Path, Mode, Error) == RL::ECheckpointResult::Ok) ++Saves; else Errors += " save:" + Error;
            if (L->LoadCheckpoint(Path, Error) == RL::ECheckpointResult::Ok) ++Loads; else Errors += " load:" + Error;
            std::vector<float> Out;
            if (!Outputs(*L, P, Out) || !AllFinite(Out)) ++NonFinite;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Done = true;
        Trainer.join();
        Decider.join();
        Report(Saves == 5 && Loads == 5 && NonFinite == 0 && Evaluations > 0, P.Id, "concurrency",
            "saves=" + Num(Saves) + "/5 loads=" + Num(Loads) + "/5 evaluations=" + Num(Evaluations) + " nonfinite=" + Num(NonFinite)
            + " bursts=" + Num(Bursts) + " generation=" + Num(L->ReadStats().Generation) + Errors);
    }
}
}

/** Regressions from review: header integrity, uncapped Stored after FullResume, no random warm-up after load. */
void RunRegressions(const RL::FProfile& P)
{
    const bool OffPolicy = P.Algorithm != RL::EAlgorithm::PPO;
    std::string Error;
    {
        // The header's Version lives at byte 152 (after magic, five uint32s, profile id, revision, padding, fingerprint).
        auto L = Make(P, MakeSettings(P, 21));
        FFeed F;
        if (L && TrainUntil(*L, P, 2, F))
        {
            const std::string Path = PathFor(P, "header-src"), Bad = PathFor(P, "header-flip");
            std::vector<unsigned char> Bytes;
            if (L->SaveCheckpoint(Path, RL::ECheckpointMode::TrainingState, Error) == RL::ECheckpointResult::Ok && ReadBytes(Path, Bytes) && Bytes.size() > 160)
            {
                Bytes[153] ^= 0x40;
                WriteBytes(Bad, Bytes);
                ExpectRefusal(*L, P, "flipped-header-version", Bad, RL::ECheckpointResult::BadFormat, "BadFormat: checksum covers header");
            }
            else Report(false, P.Id, "refuse/flipped-header-version", "setup save failed: " + Error);
        }
    }
    if (OffPolicy && P.ReplayCapacity && P.ReplayCapacity < 6000)
    {
        // No training (LearningStarts unreachable), so storing more records than the replay capacity is cheap.
        auto S = MakeSettings(P, 22);
        S.LearningStarts = 1000000;
        S.MaxRecordsPerBurst = 1024;
        S.QueueCapacity = 4096;
        auto Saver = Make(P, S), Loader = Make(P, S);
        FFeed F;
        const unsigned Records = P.ReplayCapacity + 200;
        for (unsigned K = 0; Saver && K < Records; ++K) { Step(*Saver, P, F); if (K % 256 == 0) Drain(*Saver); }
        if (Saver) Drain(*Saver);
        const std::string Path = PathFor(P, "stored");
        if (Saver && Loader && Saver->SaveCheckpoint(Path, RL::ECheckpointMode::FullResume, Error) == RL::ECheckpointResult::Ok
            && Loader->LoadCheckpoint(Path, Error) == RL::ECheckpointResult::Ok)
        {
            const auto A = Saver->ReadStats(), B = Loader->ReadStats();
            Report(B.Stored == A.Stored && A.Stored > P.ReplayCapacity && B.ReplaySize == P.ReplayCapacity, P.Id, "stats/stored-uncapped",
                "saver stored=" + Num(A.Stored) + " loaded stored=" + Num(B.Stored) + " replay=" + Num(B.ReplaySize));
        }
        else Report(false, P.Id, "stats/stored-uncapped", "save/load failed: " + Error);
    }
    if (P.Algorithm == RL::EAlgorithm::TD3)
    {
        // With no exploration noise a stochastic TD3 action equals the actor's output, unless the random warm-up applies.
        auto S = MakeSettings(P, 23);
        S.ExplorationNoise = 0;
        auto Saver = Make(P, S), Loader = Make(P, S);
        FFeed F;
        const std::string Path = PathFor(P, "norandom");
        if (Saver && Loader && TrainUntil(*Saver, P, 2, F)
            && Saver->SaveCheckpoint(Path, RL::ECheckpointMode::PolicyExport, Error) == RL::ECheckpointResult::Ok
            && Loader->LoadCheckpoint(Path, Error) == RL::ECheckpointResult::Ok)
        {
            bool Same = true;
            for (unsigned K = 0; K < 8 && Same; ++K)
            {
                RL::FDecision D;
                std::vector<float> Expected;
                Same = Loader->Decide(Obs(P, 500 + K), 9, 0, false, D) && Loader->CapturePolicy()->Evaluate(Obs(P, 500 + K), Expected)
                    && BitEqual(D.GetAction(), Expected);
            }
            Report(Same, P.Id, "load/no-random-warmup", Same ? "first stochastic actions come from the loaded actor" : "first actions after load were random");
        }
        else Report(false, P.Id, "load/no-random-warmup", "setup failed: " + Error);
    }
}

int main(int Argc, char** Argv)
{
    Root = Argc > 1 ? Argv[1] : "Saved/ScholaRLToolsNative/checkpoints";
    std::error_code Ec;
    fs::create_directories(Root, Ec);
    if (Ec) { std::printf("FAIL setup: cannot create %s\n", Root.c_str()); return 2; }
    std::atomic<bool> Finished{false};
    std::thread([&Finished]
    {
        for (int S = 0; S < 900 && !Finished; ++S) std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!Finished) { std::printf("FAIL watchdog: suite exceeded 900 s (deadlock?)\n"); std::fflush(stdout); std::_Exit(3); }
    }).detach();

    const auto Profiles = RL::FLearner::GetProfiles();
    std::printf("Schola RLtools checkpoint tests: %zu profiles, dir %s\n", Profiles.size(), Root.c_str());
    for (auto R : { RL::ECheckpointResult::Ok, RL::ECheckpointResult::IoError, RL::ECheckpointResult::BadFormat,
                    RL::ECheckpointResult::Mismatch, RL::ECheckpointResult::Unsupported, RL::ECheckpointResult::NotReady })
        Report(RL::ToString(R) && *RL::ToString(R), "-", "tostring", Res(R));
    for (const auto& P : Profiles) RunProfile(P, Profiles);
    for (const auto& P : Profiles) RunRegressions(P);
    Finished = true;
    std::printf("\nTOTAL: %d passed, %d failed, %d skipped\n", Passed, Failed, Skipped);
    for (const auto& F : Failures) std::printf("  FAILED %s\n", F.c_str());
    return Failed ? 1 : 0;
}
