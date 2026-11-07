// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#ifdef UE_MODULE_NAME
#include "HAL/Platform.h"
#endif
#include <type_traits>
#include <rl_tools/operations/cpu_mux.h>
#include <rl_tools/nn/optimizers/adam/instance/operations_generic.h>
#include <rl_tools/nn/operations_cpu_mux.h>
#include <rl_tools/nn/layers/sample_and_squash/operations_generic.h>
#include <rl_tools/nn_models/mlp/operations_generic.h>
#include <rl_tools/nn_models/sequential/operations_generic.h>
#include <rl_tools/nn/optimizers/adam/operations_generic.h>
#include <rl_tools/rl/environments/pendulum/operations_cpu.h>
#include "ChaseEnvironment.h"
#include "SpacesEnvironment.h"
#include <rl_tools/rl/algorithms/sac/loop/core/config.h>
#include <rl_tools/rl/algorithms/sac/loop/core/operations_generic.h>
#include <rl_tools/rl/algorithms/sac/operations_generic.h>

namespace online_sac::detail {
namespace rlt = rl_tools;
using Device = rlt::devices::DEVICE_FACTORY<>;
using Rng = Device::SPEC::RANDOM::ENGINE<>;
using TI = Device::index_t;
using T = float;
using TypePolicy = rlt::numeric_types::Policy<T>;
using ChaseSpec = rlt::rl::environments::chase::Specification<T, TI>;
using ChaseEnv = rlt::rl::environments::Chase<ChaseSpec>;
using ChaseParams = ChaseEnv::Parameters;

// Chase v2 learner settings (compile time: RLtools template arguments). Chase v1 (ChaseEnv) keeps the RLtools defaults
// so its results do not change. Each can be overridden with -DONLINE_SAC_<NAME>=value (used for the learning-speed sweep).
#ifndef ONLINE_SAC_GAMMA
#define ONLINE_SAC_GAMMA 0.99
#endif
#ifndef ONLINE_SAC_BATCH
#define ONLINE_SAC_BATCH 32
#endif
#ifndef ONLINE_SAC_TAU
#define ONLINE_SAC_TAU 0.005       // target network update rate (polyak = 1 - tau)
#endif
#ifndef ONLINE_SAC_LR
#define ONLINE_SAC_LR 0.001
#endif
#ifndef ONLINE_SAC_HIDDEN
#define ONLINE_SAC_HIDDEN 64
#endif
#ifndef ONLINE_SAC_ALPHA
#define ONLINE_SAC_ALPHA 0.5       // initial entropy weight (adapts)
#endif
#ifndef ONLINE_SAC_REPLAY
#define ONLINE_SAC_REPLAY 60000
#endif
template <typename ENVIRONMENT>
constexpr bool IsChaseV1 = std::is_same_v<ENVIRONMENT, ChaseEnv>;
template <typename ENVIRONMENT>
struct ChaseV2SACParameters : rlt::rl::algorithms::sac::DefaultParameters<TypePolicy, TI, ENVIRONMENT::ACTION_DIM> {
    static constexpr T GAMMA = ONLINE_SAC_GAMMA;
    static constexpr TI ACTOR_BATCH_SIZE = ONLINE_SAC_BATCH;
    static constexpr TI CRITIC_BATCH_SIZE = ONLINE_SAC_BATCH;
    static constexpr T ACTOR_POLYAK = 1.0 - ONLINE_SAC_TAU;
    static constexpr T CRITIC_POLYAK = 1.0 - ONLINE_SAC_TAU;
    static constexpr T ALPHA = ONLINE_SAC_ALPHA;
};
struct ChaseV2OptimizerParameters : rlt::nn::optimizers::adam::DEFAULT_PARAMETERS_TENSORFLOW<TypePolicy> {
    static constexpr T ALPHA = ONLINE_SAC_LR;
};
template <typename ENVIRONMENT>
struct SACLoopParameters : rlt::rl::algorithms::sac::loop::core::DefaultParameters<TypePolicy, TI, ENVIRONMENT> {
    using Base = rlt::rl::algorithms::sac::loop::core::DefaultParameters<TypePolicy, TI, ENVIRONMENT>;
    using SAC_PARAMETERS = std::conditional_t<IsChaseV1<ENVIRONMENT>, typename Base::SAC_PARAMETERS, ChaseV2SACParameters<ENVIRONMENT>>;
    using ACTOR_OPTIMIZER_PARAMETERS = std::conditional_t<IsChaseV1<ENVIRONMENT>, typename Base::ACTOR_OPTIMIZER_PARAMETERS, ChaseV2OptimizerParameters>;
    using CRITIC_OPTIMIZER_PARAMETERS = std::conditional_t<IsChaseV1<ENVIRONMENT>, typename Base::CRITIC_OPTIMIZER_PARAMETERS, ChaseV2OptimizerParameters>;
    static constexpr TI ACTOR_HIDDEN_DIM = IsChaseV1<ENVIRONMENT> ? 64 : ONLINE_SAC_HIDDEN;
    static constexpr TI CRITIC_HIDDEN_DIM = IsChaseV1<ENVIRONMENT> ? 64 : ONLINE_SAC_HIDDEN;
    static constexpr TI STEP_LIMIT = 1000000;
    static constexpr TI REPLAY_BUFFER_CAP = IsChaseV1<ENVIRONMENT> ? 60000 : ONLINE_SAC_REPLAY;
    static constexpr TI N_WARMUP_STEPS = 1000;
    static constexpr TI N_WARMUP_STEPS_CRITIC = 1000;
    static constexpr TI N_WARMUP_STEPS_ACTOR = 1000;
    static constexpr bool SAMPLE_ENVIRONMENT_PARAMETERS = false;
};
template <typename ENVIRONMENT>
using SACConfig = rlt::rl::algorithms::sac::loop::core::Config<TypePolicy, TI, Rng, ENVIRONMENT, SACLoopParameters<ENVIRONMENT>>;
template <typename ENVIRONMENT>
using SACState = typename SACConfig<ENVIRONMENT>::template State<SACConfig<ENVIRONMENT>>;
using ChaseSACConfig = SACConfig<ChaseEnv>;
using ChaseSACState = SACState<ChaseEnv>;

template <TI Width>
using RowTensor = rlt::Tensor<rlt::tensor::Specification<T, TI, rlt::tensor::Shape<TI, 1, 1, Width>, false>>;
}
