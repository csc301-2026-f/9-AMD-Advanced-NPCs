// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#ifdef UE_MODULE_NAME
#include "HAL/Platform.h"
#endif
#include <rl_tools/operations/cpu_mux.h>
#include <rl_tools/nn/optimizers/adam/instance/operations_generic.h>
#include <rl_tools/nn/operations_cpu_mux.h>
#include <rl_tools/nn/layers/sample_and_squash/operations_generic.h>
#include <rl_tools/nn_models/mlp/operations_generic.h>
#include <rl_tools/nn_models/sequential/operations_generic.h>
#include <rl_tools/nn/optimizers/adam/operations_generic.h>
#include <rl_tools/rl/environments/pendulum/operations_cpu.h>
#include "ChaseEnvironment.h"
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

template <typename ENVIRONMENT>
struct SACLoopParameters : rlt::rl::algorithms::sac::loop::core::DefaultParameters<TypePolicy, TI, ENVIRONMENT> {
    static constexpr TI STEP_LIMIT = 1000000;
    static constexpr TI REPLAY_BUFFER_CAP = 60000;
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
