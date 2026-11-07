// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Test 8: native timing of OnlineSACLearner (Act latency, round time, Act under contention). Usage: osac_timing [outfile]
#include "OnlineSACLearner.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>
#include <string>
#include <vector>

using namespace online_sac;
namespace {
using Clock = std::chrono::steady_clock;
struct Summary { double median = 0, p95 = 0, max = 0; size_t n = 0; };
Summary Summarize(std::vector<double> v) {
    Summary s; s.n = v.size();
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.median = v[v.size() / 2]; s.p95 = v[std::min(v.size() - 1, v.size() * 95 / 100)]; s.max = v.back();
    return s;
}
std::string Fmt(const char* name, const Summary& s, const char* unit) {
    char b[256];
    std::snprintf(b, sizeof b, "%-34s n=%-6zu median=%.5f p95=%.5f max=%.5f %s\n", name, s.n, s.median, s.p95, s.max, unit);
    return b;
}
std::string Shell(const char* cmd) {
    std::string r; char b[256];
    if (FILE* f = popen(cmd, "r")) { while (std::fgets(b, sizeof b, f)) r += b; pclose(f); }
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    return r;
}
struct Feeder {
    std::mt19937 rng{7};
    std::uniform_real_distribution<float> u{-1.f, 1.f};
    void Obs(float* o) { for (unsigned i = 0; i < ObservationDim; ++i) o[i] = u(rng); }
    bool Add(OnlineSACLearner& l) {
        float o[ObservationDim], n[ObservationDim], a[ActionDim];
        Obs(o); Obs(n); for (float& x : a) x = u(rng);
        return l.AddExperience(o, ObservationDim, a, ActionDim, u(rng), n, ObservationDim, false, false) == AddResult::Accepted;
    }
};
double ActMs(const OnlineSACLearner& l, const float* o, bool det) {
    float a[ActionDim];
    const auto t0 = Clock::now();
    l.Act(o, ObservationDim, a, ActionDim, det);
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
}

int main(int argc, char** argv) {
    constexpr unsigned UpdateEvery = 50, Warmup = 256, ActCalls = 10000, Rounds = 20, ContentionRounds = 5;
    const char* outPath = argc > 1 ? argv[1] : nullptr;
    OnlineSACLearner learner(OnlineSACConfig{1, 4096, UpdateEvery, Warmup, false});
    Feeder feed;
    std::string report;
    bool ok = true;

    // Warmup and first published version.
    for (unsigned i = 0; i < Warmup + UpdateEvery; ++i) feed.Add(learner);
    learner.WaitForLearner(0, 60000);
    for (int i = 0; i < 600 && learner.ReadStats().version == 0; ++i) { feed.Add(learner); learner.WaitForLearner(0, 1000); }
    const OnlineSACStats s0 = learner.ReadStats();
    if (s0.version == 0) { std::fputs("FAIL no version published\n", stdout); return 1; }

    // (a) Act latency with an idle learner.
    std::vector<double> det, smp;
    float o[ObservationDim];
    for (unsigned i = 0; i < ActCalls; ++i) { feed.Obs(o); det.push_back(ActMs(learner, o, true)); }
    for (unsigned i = 0; i < ActCalls; ++i) { feed.Obs(o); smp.push_back(ActMs(learner, o, false)); }
    const Summary sd = Summarize(det), ss = Summarize(smp);

    // (b) Round time at updateEvery = 50.
    std::vector<double> roundMs;
    unsigned rounds = learner.ReadStats().rounds;
    const uint64_t updates0 = learner.ReadStats().updates;
    const auto tb0 = Clock::now();
    for (unsigned r = 0; r < Rounds; ++r) {
        for (unsigned i = 0; i < UpdateEvery; ++i) feed.Add(learner);
        learner.WaitForLearner(0, 60000);
        const OnlineSACStats st = learner.ReadStats();
        if (st.rounds > rounds) roundMs.push_back(st.lastRoundMs);
        rounds = st.rounds;
    }
    const double wallB = std::chrono::duration<double, std::milli>(Clock::now() - tb0).count();
    const OnlineSACStats sb = learner.ReadStats();
    const Summary sr = Summarize(roundMs);
    std::vector<double> stepMs; for (double m : roundMs) stepMs.push_back(m / UpdateEvery);
    const Summary sg = Summarize(stepMs);

    // (c) Act latency while a round runs.
    std::vector<double> cdet, csmp;
    unsigned observedTraining = 0;
    for (unsigned r = 0; r < ContentionRounds; ++r) {
        const unsigned before = learner.ReadStats().rounds;
        for (unsigned i = 0; i < UpdateEvery; ++i) feed.Add(learner);
        const auto deadline = Clock::now() + std::chrono::seconds(30);
        for (unsigned k = 0; Clock::now() < deadline; ++k) {
            OnlineSACStats st = learner.ReadStats();
            if (st.rounds > before && !st.training) break;
            if (!st.training) continue;
            ++observedTraining;
            for (unsigned j = 0; j < 32; ++j) { feed.Obs(o); (j & 1 ? csmp : cdet).push_back(ActMs(learner, o, !(j & 1))); }
        }
    }
    const Summary scd = Summarize(cdet), scs = Summarize(csmp);
    learner.Shutdown();

    char b[512];
    std::time_t now = std::time(nullptr); char date[64]; std::strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S %Z", std::localtime(&now));
    report += "Test 8: OnlineSACLearner native timing\n";
    report += "machine:  " + Shell("sysctl -n machdep.cpu.brand_string") + " (" + Shell("sysctl -n hw.ncpu") + " logical CPUs), " + Shell("sw_vers -productName") + " " + Shell("sw_vers -productVersion") + "\n";
    report += std::string("compiler: Apple clang ") + __clang_version__ + "\n";
    report += "flags:    -std=c++20 -O2 -fno-exceptions -fno-rtti -DNOMINMAX -DRL_TOOLS_DEBUG\n";
    report += std::string("date:     ") + date + "\n";
    report += "NOTE: these are macOS/clang numbers, not the Windows/MSVC build used by Unreal.\n";
    std::snprintf(b, sizeof b, "config:   seed=1 queueCapacity=4096 updateEvery=%u warmupExperiences=%u; synthetic experiences (6 obs, 2 actions, uniform [-1,1])\n\n", UpdateEvery, Warmup);
    report += b;
    std::snprintf(b, sizeof b, "(a) Act latency, idle learner, version=%u\n", s0.version); report += b;
    report += Fmt("Act deterministic", sd, "ms");
    report += Fmt("Act sampled", ss, "ms");
    std::snprintf(b, sizeof b, "\n(b) Training rounds at updateEvery=%u (%zu rounds, %llu gradient steps, wall %.1f ms)\n", UpdateEvery, roundMs.size(),
        static_cast<unsigned long long>(sb.updates - updates0), wallB); report += b;
    report += Fmt("round time (lastRoundMs)", sr, "ms");
    report += Fmt("per gradient step (round/50)", sg, "ms");
    const double headroom = sr.median > 0 ? 5000.0 / sr.median : 0, headroomMax = sr.max > 0 ? 5000.0 / sr.max : 0;
    std::snprintf(b, sizeof b, "headroom 5000/round ms: %.1fx at median, %.1fx at max\n", headroom, headroomMax); report += b;
    std::snprintf(b, sizeof b, "\n(c) Act latency while a round runs (%u rounds, %u training polls)\n", ContentionRounds, observedTraining); report += b;
    report += Fmt("Act deterministic (contention)", scd, "ms");
    report += Fmt("Act sampled (contention)", scs, "ms");

    report += "\nVerdicts (targets: Act median and p95 < 0.1 ms; 50-step round < 5000 ms)\n";
    const auto verdict = [&](const char* name, bool pass, const std::string& detail) {
        report += std::string(pass ? "PASS " : "FAIL ") + name + ": " + detail + "\n"; ok = ok && pass;
    };
    const auto actOk = [](const Summary& s) { return s.n > 0 && s.median < 0.1 && s.p95 < 0.1; };
    const auto actDetail = [&](const Summary& s) { std::snprintf(b, sizeof b, "median %.5f ms, p95 %.5f ms", s.median, s.p95); return std::string(b); };
    verdict("Act deterministic idle", actOk(sd) && sd.n >= ActCalls, actDetail(sd));
    verdict("Act sampled idle", actOk(ss) && ss.n >= ActCalls, actDetail(ss));
    std::snprintf(b, sizeof b, "median %.1f ms, max %.1f ms over %zu rounds, headroom %.1fx", sr.median, sr.max, roundMs.size(), headroom);
    verdict("50-step round", roundMs.size() >= Rounds && sr.max < 5000.0, b);
    verdict("Act deterministic during round", actOk(scd), actDetail(scd));
    verdict("Act sampled during round", actOk(scs), actDetail(scs));

    std::fputs(report.c_str(), stdout);
    if (outPath) if (FILE* f = std::fopen(outPath, "w")) { std::fputs(report.c_str(), f); std::fclose(f); }
    return ok ? 0 : 1;
}
