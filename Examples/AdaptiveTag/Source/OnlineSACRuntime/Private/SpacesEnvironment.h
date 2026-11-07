// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// A spaces-only RLtools environment: it exists only to give the SAC templates their observation and
// action sizes. The push learner never steps it (experiences come from our own C++ simulation).
#pragma once
#include <rl_tools/rl/environments/environments.h>

RL_TOOLS_NAMESPACE_WRAPPER_START
namespace rl_tools::rl::environments {
    namespace spaces {
        template <typename TI, TI T_DIM>
        struct Observation {
            static constexpr TI DIM = T_DIM;
        };
        struct State {
            static constexpr int DIM = 0;
        };
        struct Parameters {};
    }
    template <typename T_T, typename T_TI, T_TI OBSERVATION_DIM, T_TI T_ACTION_DIM, T_TI T_EPISODE_STEP_LIMIT>
    struct Spaces : Environment<T_T, T_TI> {
        using T = T_T;
        using TI = T_TI;
        using State = spaces::State;
        using Parameters = spaces::Parameters;
        using Observation = spaces::Observation<TI, OBSERVATION_DIM>;
        using ObservationPrivileged = Observation;
        static constexpr TI N_AGENTS = 1;
        static constexpr TI ACTION_DIM = T_ACTION_DIM;
        static constexpr TI EPISODE_STEP_LIMIT = T_EPISODE_STEP_LIMIT; // only sizes the unused runner episode stats
    };
}
namespace rl_tools {
    template <typename DEVICE, typename T, typename TI, TI O, TI A, TI L>
    RL_TOOLS_FUNCTION_PLACEMENT static void malloc(DEVICE&, const rl::environments::Spaces<T, TI, O, A, L>&) { }
    template <typename DEVICE, typename T, typename TI, TI O, TI A, TI L>
    RL_TOOLS_FUNCTION_PLACEMENT static void free(DEVICE&, const rl::environments::Spaces<T, TI, O, A, L>&) { }
    template <typename DEVICE, typename T, typename TI, TI O, TI A, TI L>
    RL_TOOLS_FUNCTION_PLACEMENT static void init(DEVICE&, const rl::environments::Spaces<T, TI, O, A, L>&) { }
    template <typename DEVICE, typename T, typename TI, TI O, TI A, TI L>
    RL_TOOLS_FUNCTION_PLACEMENT static void initial_parameters(DEVICE&, const rl::environments::Spaces<T, TI, O, A, L>&, rl::environments::spaces::Parameters&) { }
}
RL_TOOLS_NAMESPACE_WRAPPER_END
