// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ChaseSession.h"
#include "OnlineSACLearner.h"
#include <deque>

namespace online_sac {
namespace {
constexpr unsigned O = ChaseObservationSize;
constexpr unsigned A = ChaseActionSize;
constexpr unsigned LearnerWaitMs = 60000;
// Separate seed families so training, evaluation, playback and random-baseline actions never share a stream.
uint64_t SeedFor(unsigned seed, uint64_t family, unsigned index) { return (static_cast<uint64_t>(seed) << 32) ^ (family * 0x9E3779B97F4A7C15ull) ^ index; }
constexpr uint64_t TrainFamily = 1, EvalFamily = 2, PlayFamily = 3, RandomActionFamily = 4;

using ChasePolicyFn = std::function<void(const float* observation, float* action, ChaseRng& rng)>;
// One episode in its own world, no learning. Returns the trajectory (frames only when `record`).
ChaseTrajectory RunEpisode(const ChaseConfig& config, RunnerPattern pattern, ChaseRng& startRng, ChaseRng& actionRng, const ChasePolicyFn& policy, bool record) {
    ChaseWorld world;
    world.Reset(config, pattern, startRng);
    ChaseTrajectory trajectory;
    trajectory.pattern = pattern;
    if (record) trajectory.frames.push_back(world.State());
    float observation[O], action[A];
    for (;;) {
        world.Observe(observation);
        policy(observation, action, actionRng);
        const ChaseStepOutcome outcome = world.Step(action);
        if (record) trajectory.frames.push_back(world.State());
        if (outcome.terminated || outcome.truncated) break;
    }
    trajectory.caught = world.State().caught;
    trajectory.steps = world.State().step;
    if (!record) trajectory.frames.push_back(world.State());
    return trajectory;
}
}

struct ChaseSession::Impl {
    ChaseConfig config;
    ChaseLearnerConfig learnerConfig;
    RunnerPattern pattern;
    std::unique_ptr<ChaseLearner> learner;
    ChaseWorld world;
    ChaseRng rng;
    bool frozen = false;
    std::shared_ptr<const ChaseLearner::Policy> snapshot;
    uint64_t totalSteps = 0;
    unsigned episodesDone = 0;
    float episodeReward = 0;
    std::deque<EpisodeSummary> recent;
    ChaseTrajectory current, last;

    void NewLearner() {
        learner.reset();
        OnlineSACConfig c;
        c.seed = learnerConfig.seed;
        c.updateEvery = learnerConfig.update_every;
        c.warmupExperiences = learnerConfig.warmup_experiences;
        c.queueCapacity = learnerConfig.queue_capacity;
        c.frozen = frozen;
        learner = std::make_unique<ChaseLearner>(c);
        rng = ChaseRng(SeedFor(learnerConfig.seed, TrainFamily, 0));
        totalSteps = 0;
        episodesDone = 0;
        recent.clear();
        last = {};
        StartEpisode();
    }
    void StartEpisode() {
        world.Reset(config, pattern, rng);
        episodeReward = 0;
        current = {};
        current.pattern = pattern;
        current.frames.push_back(world.State());
    }
    void TrimStats() {
        while (recent.size() > config.stats_window) recent.pop_front();
    }
};

ChaseSession::ChaseSession(const ChaseConfig& config, const ChaseLearnerConfig& learner, RunnerPattern pattern, std::string* error)
    : data(std::make_unique<Impl>()) {
    const std::string problem = ValidateConfig(config, learner);
    if (error) *error = problem;
    if (problem.empty()) { data->config = config; data->learnerConfig = learner; }
    data->pattern = pattern;
    data->NewLearner();
}
ChaseSession::~ChaseSession() = default;

ChaseStepResult ChaseSession::Step() {
    Impl& d = *data;
    ChaseStepResult result;
    float observation[O], next[O], action[A];
    d.world.Observe(observation);
    unsigned version = 0;
    d.learner->Act(observation, O, action, A, false, &version);
    for (float& a : action) a = a < -1 ? -1 : (a > 1 ? 1 : a);
    result.outcome = d.world.Step(action);
    d.world.Observe(next);
    d.learner->AddExperience(observation, O, action, A, result.outcome.reward, next, O, result.outcome.terminated, result.outcome.truncated);
    if (d.learnerConfig.lockstep) d.learner->WaitForLearner(d.learnerConfig.update_every - 1, LearnerWaitMs);
    ++d.totalSteps;
    d.episodeReward += result.outcome.reward;
    d.current.frames.push_back(d.world.State());
    d.current.policyVersion = version;
    if (result.outcome.terminated || result.outcome.truncated) {
        const ChaseWorldState& end = d.world.State();
        EpisodeSummary& s = result.summary;
        s.episode = ++d.episodesDone;
        s.pattern = d.pattern;
        s.caught = end.caught;
        s.steps = end.step;
        s.finalDistance = end.distance;
        s.totalReward = d.episodeReward;
        d.recent.push_back(s);
        d.TrimStats();
        d.current.caught = end.caught;
        d.current.steps = end.step;
        d.last = std::move(d.current);
        result.episodeEnded = true;
        d.StartEpisode();
    }
    return result;
}

void ChaseSession::RunEpisodes(unsigned episodes, const std::function<void(const EpisodeSummary&)>& onEpisode) {
    for (unsigned done = 0; done < episodes;) {
        const ChaseStepResult r = Step();
        if (!r.episodeEnded) continue;
        ++done;
        if (onEpisode) onEpisode(r.summary);
    }
}

void ChaseSession::SetPattern(RunnerPattern pattern) {
    data->pattern = pattern;
    data->StartEpisode();
}
void ChaseSession::SetFrozen(bool frozen) {
    data->frozen = frozen;
    data->learner->SetFrozen(frozen);
}
void ChaseSession::TakeSnapshot() { data->snapshot = data->learner->CurrentPolicy(); }
void ChaseSession::Reset() { data->NewLearner(); }

ChaseEvaluation ChaseSession::Evaluate(unsigned episodes) const {
    const Impl& d = *data;
    ChaseEvaluation e;
    e.pattern = d.pattern;
    const std::shared_ptr<const ChaseLearner::Policy> currentPolicy = d.learner->CurrentPolicy();
    e.currentVersion = ChaseLearner::VersionOf(*currentPolicy);
    e.hasSnapshot = d.snapshot != nullptr;
    if (e.hasSnapshot) e.snapshotVersion = ChaseLearner::VersionOf(*d.snapshot);
    auto run = [&](const ChasePolicyFn& policy) {
        ChaseEvalResult r;
        double steps = 0, distance = 0;
        for (unsigned i = 0; i < episodes; ++i) {
            ChaseRng startRng(SeedFor(d.config.eval_seed, EvalFamily, i)), actionRng(SeedFor(d.config.eval_seed, RandomActionFamily, i));
            const ChaseTrajectory t = RunEpisode(d.config, d.pattern, startRng, actionRng, policy, false);
            ++r.episodes;
            if (t.caught) { ++r.caught; steps += t.steps; }
            distance += t.frames.back().distance;
        }
        r.catchRate = r.episodes ? static_cast<double>(r.caught) / r.episodes : 0;
        r.meanStepsToCatch = r.caught ? steps / r.caught : 0;
        r.meanFinalDistance = r.episodes ? distance / r.episodes : 0;
        return r;
    };
    auto learned = [](const ChaseLearner::Policy& policy) {
        const ChaseLearner::Policy* p = &policy;
        return ChasePolicyFn([p](const float* observation, float* action, ChaseRng&) { ChaseLearner::ActWith(*p, observation, O, action, A); });
    };
    e.current = run(learned(*currentPolicy));
    if (e.hasSnapshot) e.snapshot = run(learned(*d.snapshot));
    e.greedy = run([](const float* observation, float* action, ChaseRng&) { GreedyChaseAction(observation, action); });
    e.random = run([](const float*, float* action, ChaseRng& rng) { for (unsigned i = 0; i < A; ++i) action[i] = rng.Uniform(-1, 1); });
    return e;
}

ChaseTrajectory ChaseSession::PlayEpisode(unsigned index) const {
    const Impl& d = *data;
    const std::shared_ptr<const ChaseLearner::Policy> policy = d.learner->CurrentPolicy();
    ChaseRng startRng(SeedFor(d.config.eval_seed, PlayFamily, index)), unused(0);
    ChaseTrajectory t = RunEpisode(d.config, d.pattern, startRng, unused,
        [&](const float* observation, float* action, ChaseRng&) { ChaseLearner::ActWith(*policy, observation, O, action, A); }, true);
    t.policyVersion = ChaseLearner::VersionOf(*policy);
    return t;
}
const ChaseTrajectory& ChaseSession::LastTrainingEpisode() const { return data->last; }

std::string ChaseSession::SetConfig(const ChaseConfig& config) {
    const std::string error = ValidateConfig(config, data->learnerConfig);
    if (!error.empty()) return error;
    data->config = config;
    data->TrimStats();
    data->StartEpisode();
    return "";
}
std::string ChaseSession::SetLearnerConfig(const ChaseLearnerConfig& learner) {
    const std::string error = ValidateConfig(data->config, learner);
    if (error.empty()) data->learnerConfig = learner;
    return error;
}
const ChaseConfig& ChaseSession::Config() const { return data->config; }
const ChaseLearnerConfig& ChaseSession::LearnerConfig() const { return data->learnerConfig; }

ChaseSessionState ChaseSession::GetState() const {
    const Impl& d = *data;
    ChaseSessionState s;
    s.world = d.world.State();
    s.pattern = d.pattern;
    s.totalSteps = d.totalSteps;
    s.episodesDone = d.episodesDone;
    s.policyVersion = d.learner->Version();
    s.updates = d.learner->ReadStats().updates;
    s.frozen = d.frozen;
    s.hasSnapshot = d.snapshot != nullptr;
    s.snapshotVersion = s.hasSnapshot ? ChaseLearner::VersionOf(*d.snapshot) : 0;
    double steps = 0, distance = 0;
    for (const EpisodeSummary& e : d.recent) {
        ++s.stats.episodes;
        if (e.caught) { ++s.stats.caught; steps += e.steps; }
        distance += e.finalDistance;
    }
    if (s.stats.episodes) {
        s.stats.catchRate = static_cast<double>(s.stats.caught) / s.stats.episodes;
        s.stats.meanFinalDistance = distance / s.stats.episodes;
    }
    if (s.stats.caught) s.stats.meanStepsToCatch = steps / s.stats.caught;
    return s;
}
}
