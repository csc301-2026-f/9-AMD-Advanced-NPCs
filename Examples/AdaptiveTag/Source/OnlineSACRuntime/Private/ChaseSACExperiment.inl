// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ChaseSACExperiment.h"
#include <chrono>
#include <cstdio>
#include <memory>

namespace online_sac {
using namespace detail;
namespace {
using Clock = std::chrono::steady_clock;
double Seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

template <typename STATE>
float RecentTrainReturn(STATE& ts) {
    auto& stats = rlt::get(ts.off_policy_runner.episode_stats, 0, 0);
    const TI finished = stats.next_episode_i > 0 ? stats.next_episode_i - 1 : 0;
    const TI count = finished < 10 ? finished : 10;
    float sum = 0;
    for (TI i = finished - count; i < finished; ++i) sum += rlt::get(stats.returns, i, 0);
    return count ? sum / count : 0.0f;
}

template <typename STATE>
EvalResult EvaluateActor(Device& device, STATE& ts, Rng& rng, ChasePattern pattern, const ChaseSACOptions& options) {
    RowTensor<ObservationDim> input;
    RowTensor<ActionDim> output;
    auto policy = [&](const float* observation, float* action) {
        for (TI i = 0; i < ObservationDim; ++i) rlt::set(device, input, observation[i], 0, 0, i);
        rlt::evaluate(device, ts.actor_critic.actor, input, output, ts.actor_buffers_eval, rng, rlt::Mode<rlt::mode::Evaluation<>>{});
        for (TI i = 0; i < ActionDim; ++i) action[i] = rlt::get(device, output, 0, 0, i);
    };
    return EvaluatePolicy(policy, pattern, options.evalEpisodes, options.evalSeed);
}
}

const CurvePoint* ChaseSACResult::AfterTraining() const {
    const CurvePoint* last = nullptr;
    for (const CurvePoint& point : curve) {
        if (point.trainingPattern == static_cast<unsigned>(ChasePattern::Bounce)) last = &point;
    }
    return last;
}

ChaseSACResult RunChaseSAC(const ChaseSACOptions& options, const std::atomic<bool>* cancel) {
    ChaseSACResult result;
    result.seed = options.seed;
    Device device;
    Rng evalRng;
    rlt::malloc(device, evalRng);
    rlt::init(device, evalRng, options.seed + 1000);
    auto ts = std::make_unique<ChaseSACState>();
    rlt::malloc(device, *ts);
    rlt::init(device, *ts, options.seed);
    ChaseParams parameters;
    parameters.pattern = static_cast<TI>(ChasePattern::Bounce);
    rlt::set_parameters(device, ts->off_policy_runner, parameters);

    const auto start = Clock::now();
    auto record = [&](unsigned step, ChasePattern training) {
        CurvePoint point;
        point.step = step;
        point.trainingPattern = static_cast<unsigned>(training);
        point.bounce = EvaluateActor(device, *ts, evalRng, ChasePattern::Bounce, options);
        point.circle = EvaluateActor(device, *ts, evalRng, ChasePattern::Circle, options);
        point.trainReturn = RecentTrainReturn(*ts);
        point.seconds = Seconds(start);
        result.curve.push_back(point);
    };
    auto stopped = [&] { return cancel && cancel->load(); };
    record(0, ChasePattern::Bounce);
    unsigned step = 0;
    for (; step < options.trainSteps && !stopped(); ) {
        rlt::step(device, *ts);
        if (++step % options.evalInterval == 0) record(step, ChasePattern::Bounce);
    }
    result.trainSeconds = Seconds(start);
    if (options.adaptSteps > 0 && !stopped()) {
        parameters.pattern = static_cast<TI>(ChasePattern::Circle);
        rlt::set_parameters(device, ts->off_policy_runner, parameters);
        rlt::truncate_all(device, ts->off_policy_runner);
        const auto adaptStart = Clock::now();
        for (unsigned adapted = 0; adapted < options.adaptSteps && !stopped(); ) {
            rlt::step(device, *ts);
            ++step;
            if (++adapted % options.evalInterval == 0) record(step, ChasePattern::Circle);
        }
        result.adaptSeconds = Seconds(adaptStart);
    }
    result.completed = !stopped();
    rlt::free(device, *ts);
    rlt::free(device, evalRng);
    return result;
}

std::string CurveToCsv(const ChaseSACResult& result) {
    std::string csv = "seed,step,training_pattern,bounce_catch_rate,bounce_mean_steps_to_catch,bounce_mean_return,circle_catch_rate,circle_mean_steps_to_catch,circle_mean_return,train_return_last10,seconds\n";
    char line[320];
    for (const CurvePoint& p : result.curve) {
        std::snprintf(line, sizeof(line), "%u,%u,%s,%.3f,%.1f,%.2f,%.3f,%.1f,%.2f,%.2f,%.1f\n",
            result.seed, p.step, p.trainingPattern == 0 ? "bounce" : "circle",
            p.bounce.catchRate, p.bounce.meanStepsToCatch, p.bounce.meanReturn,
            p.circle.catchRate, p.circle.meanStepsToCatch, p.circle.meanReturn, p.trainReturn, p.seconds);
        csv += line;
    }
    return csv;
}

PendulumSACResult RunPendulumSAC(unsigned seed, unsigned steps) {
    using PendulumEnv = rlt::rl::environments::Pendulum<rlt::rl::environments::pendulum::Specification<T, TI>>;
    PendulumSACResult result;
    Device device;
    auto ts = std::make_unique<SACState<PendulumEnv>>();
    rlt::malloc(device, *ts);
    rlt::init(device, *ts, seed);
    const auto start = Clock::now();
    for (unsigned step = 1; step <= steps; ++step) {
        rlt::step(device, *ts);
        if (step % 1000 == 0) result.returnsEvery1000.push_back(RecentTrainReturn(*ts));
    }
    result.seconds = Seconds(start);
    result.finalMeanReturn = RecentTrainReturn(*ts);
    rlt::free(device, *ts);
    return result;
}
}
