// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ChaseTask.h"
#include <algorithm>
#include <cmath>

namespace online_sac {
using namespace detail;
using EnvParameters = ChaseSpec::PARAMETERS;
static_assert(EnvParameters::BOARD_SIZE == ChaseConstants::BoardSize);
static_assert(EnvParameters::DT == ChaseConstants::Dt);
static_assert(EnvParameters::MAX_EPISODE_LENGTH == ChaseConstants::EpisodeSteps);
static_assert(EnvParameters::AGENT_MAX_SPEED == ChaseConstants::AgentMaxSpeed);
static_assert(EnvParameters::AGENT_MAX_ACCELERATION == ChaseConstants::AgentMaxAcceleration);
static_assert(EnvParameters::TARGET_MAX_SPEED == ChaseConstants::TargetMaxSpeed);
static_assert(EnvParameters::CATCH_DISTANCE == ChaseConstants::CatchDistance);
static_assert(ChaseEnv::Observation::DIM == ObservationDim && ChaseEnv::ACTION_DIM == ActionDim);

struct ChaseTask::Impl {
    Device device;
    Rng rng;
    ChaseEnv env;
    ChaseParams parameters;
    ChaseEnv::State state;
    explicit Impl(unsigned seed, ChasePattern pattern) {
        rlt::malloc(device, rng);
        rlt::init(device, rng, seed);
        rlt::malloc(device, env);
        rlt::init(device, env);
        rlt::initial_parameters(device, env, parameters);
        parameters.pattern = static_cast<TI>(pattern);
        rlt::sample_initial_state(device, env, parameters, state, rng);
    }
    ~Impl() { rlt::free(device, env); rlt::free(device, rng); }
};

ChaseTask::ChaseTask(unsigned seed, ChasePattern pattern) : data(std::make_unique<Impl>(seed, pattern)) {}
ChaseTask::~ChaseTask() = default;
void ChaseTask::SetPattern(ChasePattern pattern) { data->parameters.pattern = static_cast<TI>(pattern); }
void ChaseTask::Reset() { rlt::sample_initial_state(data->device, data->env, data->parameters, data->state, data->rng); }
unsigned ChaseTask::StepIndex() const { return static_cast<unsigned>(data->state.step); }
void ChaseTask::Observe(float* observation) const {
    rlt::Matrix<rlt::matrix::Specification<T, TI, 1, ObservationDim, false>> m;
    rlt::observe(data->device, data->env, data->parameters, data->state, ChaseEnv::Observation{}, m, data->rng);
    for (unsigned i = 0; i < ObservationDim; ++i) observation[i] = rlt::get(m, 0, i);
}
ChaseStep ChaseTask::Step(const float* action) {
    rlt::Matrix<rlt::matrix::Specification<T, TI, 1, ActionDim, false>> a;
    for (unsigned i = 0; i < ActionDim; ++i) rlt::set(a, 0, i, action[i]);
    ChaseEnv::State next;
    rlt::step(data->device, data->env, data->parameters, data->state, a, next, data->rng);
    ChaseStep result;
    result.reward = rlt::reward(data->device, data->env, data->parameters, data->state, a, next, data->rng);
    result.terminated = rlt::terminated(data->device, data->env, data->parameters, next, data->rng);
    result.truncated = result.terminated || next.step >= ChaseEnv::EPISODE_STEP_LIMIT;
    data->state = next;
    return result;
}

EvalResult EvaluatePolicy(const PolicyFn& policy, ChasePattern pattern, unsigned episodes, unsigned seed) {
    ChaseTask task(seed, pattern);
    EvalResult result;
    double stepsToCatch = 0, returns = 0;
    float observation[ObservationDim], action[ActionDim];
    for (unsigned episode = 0; episode < episodes; ++episode) {
        task.Reset();
        double episodeReturn = 0;
        for (;;) {
            task.Observe(observation);
            policy(observation, action);
            const ChaseStep step = task.Step(action);
            episodeReturn += step.reward;
            if (step.terminated) { ++result.caught; stepsToCatch += task.StepIndex(); }
            if (step.truncated) break;
        }
        returns += episodeReturn;
    }
    result.episodes = episodes;
    result.catchRate = episodes ? double(result.caught) / episodes : 0;
    result.meanStepsToCatch = result.caught ? stepsToCatch / result.caught : 0;
    result.meanReturn = episodes ? returns / episodes : 0;
    return result;
}

void GreedyPursuitAction(const float* observation, float* action) {
    const float dx = observation[0] * ChaseConstants::BoardSize, dy = observation[1] * ChaseConstants::BoardSize;
    const float vx = observation[2] * ChaseConstants::AgentMaxSpeed, vy = observation[3] * ChaseConstants::AgentMaxSpeed;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float desiredX = length > 1e-6f ? ChaseConstants::AgentMaxSpeed * dx / length : 0.0f;
    const float desiredY = length > 1e-6f ? ChaseConstants::AgentMaxSpeed * dy / length : 0.0f;
    const float scale = 1.0f / (ChaseConstants::AgentMaxAcceleration * ChaseConstants::Dt);
    action[0] = std::clamp((desiredX - vx) * scale, -1.0f, 1.0f);
    action[1] = std::clamp((desiredY - vy) * scale, -1.0f, 1.0f);
}
}
