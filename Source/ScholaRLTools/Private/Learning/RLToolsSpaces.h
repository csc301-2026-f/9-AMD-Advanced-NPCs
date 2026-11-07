// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include <rl_tools/rl/environments/environments.h>

// A numerical descriptor for template dimensions, not a second gameplay environment.
RL_TOOLS_NAMESPACE_WRAPPER_START
namespace rl_tools::rl::environments
{
namespace schola
{
template <typename TI, TI Dimension>
struct Observation { static constexpr TI DIM = Dimension; };
struct State { static constexpr int DIM = 0; };
struct Parameters {};
}

template <typename T_T, typename T_TI, T_TI Observations, T_TI Actions>
struct ScholaSpaces : Environment<T_T, T_TI>
{
    using T = T_T;
    using TI = T_TI;
    using State = schola::State;
    using Parameters = schola::Parameters;
    using Observation = schola::Observation<TI, Observations>;
    using ObservationPrivileged = Observation;
    static constexpr TI N_AGENTS = 1;
    static constexpr TI ACTION_DIM = Actions;
    // Sizes unused runner bookkeeping. Actual game episode boundaries arrive in experience records.
    static constexpr TI EPISODE_STEP_LIMIT = 1;
};
}

namespace rl_tools
{
template <typename DEVICE, typename T, typename TI, TI O, TI A>
RL_TOOLS_FUNCTION_PLACEMENT static void malloc(DEVICE&, const rl::environments::ScholaSpaces<T, TI, O, A>&) {}
template <typename DEVICE, typename T, typename TI, TI O, TI A>
RL_TOOLS_FUNCTION_PLACEMENT static void free(DEVICE&, const rl::environments::ScholaSpaces<T, TI, O, A>&) {}
template <typename DEVICE, typename T, typename TI, TI O, TI A>
RL_TOOLS_FUNCTION_PLACEMENT static void init(DEVICE&, const rl::environments::ScholaSpaces<T, TI, O, A>&) {}
template <typename DEVICE, typename T, typename TI, TI O, TI A>
RL_TOOLS_FUNCTION_PLACEMENT static void initial_parameters(DEVICE&, const rl::environments::ScholaSpaces<T, TI, O, A>&, rl::environments::schola::Parameters&) {}
}
RL_TOOLS_NAMESPACE_WRAPPER_END
