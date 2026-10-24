// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include <rl_tools/math/operations_generic.h>
#include <rl_tools/rl/environments/environments.h>

RL_TOOLS_NAMESPACE_WRAPPER_START
namespace rl_tools::rl::environments::chase {
    template <typename T, typename TI>
    struct DefaultParameters {
        constexpr static T BOARD_SIZE = 10;
        constexpr static T DT = (T)0.02;
        constexpr static TI MAX_EPISODE_LENGTH = 200;
        constexpr static T AGENT_MAX_SPEED = 5;
        constexpr static T AGENT_MAX_ACCELERATION = 20;
        constexpr static T TARGET_MAX_SPEED = 3;
        constexpr static T CATCH_DISTANCE = 0.5;
        constexpr static T MIN_START_DISTANCE = 3;
        constexpr static T CIRCLE_RADIUS_MIN = 2;
        constexpr static T CIRCLE_RADIUS_MAX = 3;
        constexpr static T CATCH_BONUS = 10;
        constexpr static TI PATTERN_BOUNCE = 0;
        constexpr static TI PATTERN_CIRCLE = 1;
        TI pattern = PATTERN_BOUNCE;
    };
    template <typename T_T, typename T_TI, typename T_PARAMETERS = DefaultParameters<T_T, T_TI>>
    struct Specification {
        using T = T_T;
        using TI = T_TI;
        using PARAMETERS = T_PARAMETERS;
    };
    template <typename TI>
    struct Observation {
        static constexpr TI DIM = 6;
    };
    template <typename T, typename TI>
    struct State {
        static constexpr TI DIM = 8;
        T agent_position[2];
        T agent_velocity[2];
        T target_position[2];
        T target_velocity[2];
        T circle_center[2];
        T circle_radius;
        T circle_angle;
        T circle_angular_velocity;
        TI step;
    };
}
RL_TOOLS_NAMESPACE_WRAPPER_END

RL_TOOLS_NAMESPACE_WRAPPER_START
namespace rl_tools::rl::environments {
    template <typename T_SPEC>
    struct Chase: Environment<typename T_SPEC::T, typename T_SPEC::TI> {
        using SPEC = T_SPEC;
        using T = typename SPEC::T;
        using TI = typename SPEC::TI;
        using State = chase::State<T, TI>;
        using Parameters = typename SPEC::PARAMETERS;
        using Observation = chase::Observation<TI>;
        using ObservationPrivileged = Observation;
        static constexpr TI N_AGENTS = 1;
        static constexpr TI ACTION_DIM = 2;
        static constexpr TI EPISODE_STEP_LIMIT = Parameters::MAX_EPISODE_LENGTH;
    };
}
RL_TOOLS_NAMESPACE_WRAPPER_END

RL_TOOLS_NAMESPACE_WRAPPER_START
namespace rl_tools {
    namespace rl::environments::chase {
        template <typename DEVICE, typename T>
        RL_TOOLS_FUNCTION_PLACEMENT T distance(DEVICE& device, const T a[2], const T b[2]) {
            const T dx = a[0] - b[0], dy = a[1] - b[1];
            return math::sqrt(device.math, dx * dx + dy * dy);
        }
        template <typename DEVICE, typename SPEC>
        RL_TOOLS_FUNCTION_PLACEMENT void set_circle_kinematics(DEVICE& device, typename Chase<SPEC>::State& state) {
            using T = typename SPEC::T;
            const T c = math::cos(device.math, state.circle_angle), s = math::sin(device.math, state.circle_angle);
            state.target_position[0] = state.circle_center[0] + state.circle_radius * c;
            state.target_position[1] = state.circle_center[1] + state.circle_radius * s;
            state.target_velocity[0] = -state.circle_angular_velocity * state.circle_radius * s;
            state.target_velocity[1] = state.circle_angular_velocity * state.circle_radius * c;
        }
    }
    template <typename DEVICE, typename SPEC>
    RL_TOOLS_FUNCTION_PLACEMENT static void malloc(DEVICE& device, const rl::environments::Chase<SPEC>& env) { }
    template <typename DEVICE, typename SPEC>
    RL_TOOLS_FUNCTION_PLACEMENT static void free(DEVICE& device, const rl::environments::Chase<SPEC>& env) { }
    template <typename DEVICE, typename SPEC>
    RL_TOOLS_FUNCTION_PLACEMENT static void init(DEVICE& device, const rl::environments::Chase<SPEC>& env) { }
    template <typename DEVICE, typename SPEC>
    RL_TOOLS_FUNCTION_PLACEMENT static void initial_parameters(DEVICE& device, const rl::environments::Chase<SPEC>& env, typename rl::environments::Chase<SPEC>::Parameters& parameters) {
        parameters.pattern = SPEC::PARAMETERS::PATTERN_BOUNCE;
    }
    template <typename DEVICE, typename SPEC, typename RNG>
    RL_TOOLS_FUNCTION_PLACEMENT static void sample_initial_parameters(DEVICE& device, const rl::environments::Chase<SPEC>& env, typename rl::environments::Chase<SPEC>::Parameters& parameters, RNG& rng) {
    }
    template <typename DEVICE, typename SPEC, typename RNG>
    RL_TOOLS_FUNCTION_PLACEMENT static void sample_initial_state(DEVICE& device, const rl::environments::Chase<SPEC>& env, typename rl::environments::Chase<SPEC>::Parameters& parameters, typename rl::environments::Chase<SPEC>::State& state, RNG& rng) {
        using T = typename SPEC::T;
        using TI = typename SPEC::TI;
        using PARAMS = typename SPEC::PARAMETERS;
        if (parameters.pattern == PARAMS::PATTERN_CIRCLE) {
            state.circle_radius = random::uniform_real_distribution(device.random, PARAMS::CIRCLE_RADIUS_MIN, PARAMS::CIRCLE_RADIUS_MAX, rng);
            for (TI i = 0; i < 2; i++) {
                state.circle_center[i] = random::uniform_real_distribution(device.random, state.circle_radius, PARAMS::BOARD_SIZE - state.circle_radius, rng);
            }
            state.circle_angle = random::uniform_real_distribution(device.random, (T)0, 2 * math::PI<T>, rng);
            const T direction = random::uniform_int_distribution(device.random, 0, 1, rng) == 0 ? (T)-1 : (T)1;
            state.circle_angular_velocity = direction * PARAMS::TARGET_MAX_SPEED / state.circle_radius;
            rl::environments::chase::set_circle_kinematics<DEVICE, SPEC>(device, state);
        } else {
            for (TI i = 0; i < 2; i++) {
                state.target_position[i] = random::uniform_real_distribution(device.random, (T)0, PARAMS::BOARD_SIZE, rng);
                state.circle_center[i] = 0;
            }
            const T heading = random::uniform_real_distribution(device.random, (T)0, 2 * math::PI<T>, rng);
            state.target_velocity[0] = PARAMS::TARGET_MAX_SPEED * math::cos(device.math, heading);
            state.target_velocity[1] = PARAMS::TARGET_MAX_SPEED * math::sin(device.math, heading);
            state.circle_radius = 0; state.circle_angle = 0; state.circle_angular_velocity = 0;
        }
        for (TI attempt = 0; attempt < 64; attempt++) {
            state.agent_position[0] = random::uniform_real_distribution(device.random, (T)0, PARAMS::BOARD_SIZE, rng);
            state.agent_position[1] = random::uniform_real_distribution(device.random, (T)0, PARAMS::BOARD_SIZE, rng);
            if (rl::environments::chase::distance(device, state.agent_position, state.target_position) >= PARAMS::MIN_START_DISTANCE) {
                break;
            }
        }
        state.agent_velocity[0] = 0;
        state.agent_velocity[1] = 0;
        state.step = 0;
    }
    template <typename DEVICE, typename SPEC>
    RL_TOOLS_FUNCTION_PLACEMENT static void initial_state(DEVICE& device, const rl::environments::Chase<SPEC>& env, typename rl::environments::Chase<SPEC>::Parameters& parameters, typename rl::environments::Chase<SPEC>::State& state) {
        using T = typename SPEC::T;
        using PARAMS = typename SPEC::PARAMETERS;
        state.agent_position[0] = PARAMS::BOARD_SIZE / 4;
        state.agent_position[1] = PARAMS::BOARD_SIZE / 4;
        state.agent_velocity[0] = 0;
        state.agent_velocity[1] = 0;
        state.circle_center[0] = PARAMS::BOARD_SIZE / 2;
        state.circle_center[1] = PARAMS::BOARD_SIZE / 2;
        state.circle_radius = PARAMS::CIRCLE_RADIUS_MAX;
        state.circle_angle = 0;
        state.circle_angular_velocity = PARAMS::TARGET_MAX_SPEED / PARAMS::CIRCLE_RADIUS_MAX;
        if (parameters.pattern == PARAMS::PATTERN_CIRCLE) {
            rl::environments::chase::set_circle_kinematics<DEVICE, SPEC>(device, state);
        } else {
            state.target_position[0] = PARAMS::BOARD_SIZE * 3 / 4;
            state.target_position[1] = PARAMS::BOARD_SIZE * 3 / 4;
            state.target_velocity[0] = PARAMS::TARGET_MAX_SPEED;
            state.target_velocity[1] = 0;
        }
        state.step = 0;
    }
    template <typename DEVICE, typename SPEC, typename ACTION_SPEC, typename RNG>
    RL_TOOLS_FUNCTION_PLACEMENT typename SPEC::T step(DEVICE& device, const rl::environments::Chase<SPEC>& env, typename rl::environments::Chase<SPEC>::Parameters& parameters, const typename rl::environments::Chase<SPEC>::State& state, const Matrix<ACTION_SPEC>& action, typename rl::environments::Chase<SPEC>::State& next_state, RNG& rng) {
        static_assert(ACTION_SPEC::ROWS == 1);
        static_assert(ACTION_SPEC::COLS == 2);
        using T = typename SPEC::T;
        using TI = typename SPEC::TI;
        using PARAMS = typename SPEC::PARAMETERS;
        next_state = state;
        for (TI i = 0; i < 2; i++) {
            const T u = math::clamp(device.math, get(action, 0, i), (T)-1, (T)1);
            next_state.agent_velocity[i] = state.agent_velocity[i] + PARAMS::AGENT_MAX_ACCELERATION * u * PARAMS::DT;
        }
        const T speed = math::sqrt(device.math, next_state.agent_velocity[0] * next_state.agent_velocity[0] + next_state.agent_velocity[1] * next_state.agent_velocity[1]);
        if (speed > PARAMS::AGENT_MAX_SPEED) {
            next_state.agent_velocity[0] *= PARAMS::AGENT_MAX_SPEED / speed;
            next_state.agent_velocity[1] *= PARAMS::AGENT_MAX_SPEED / speed;
        }
        for (TI i = 0; i < 2; i++) {
            T p = state.agent_position[i] + next_state.agent_velocity[i] * PARAMS::DT;
            if (p < 0 || p > PARAMS::BOARD_SIZE) {
                p = math::clamp(device.math, p, (T)0, PARAMS::BOARD_SIZE);
                next_state.agent_velocity[i] = 0;
            }
            next_state.agent_position[i] = p;
        }
        if (parameters.pattern == PARAMS::PATTERN_CIRCLE) {
            next_state.circle_angle = state.circle_angle + state.circle_angular_velocity * PARAMS::DT;
            rl::environments::chase::set_circle_kinematics<DEVICE, SPEC>(device, next_state);
        } else {
            for (TI i = 0; i < 2; i++) {
                T p = state.target_position[i] + state.target_velocity[i] * PARAMS::DT;
                T v = state.target_velocity[i];
                if (p < 0) { p = -p; v = -v; }
                if (p > PARAMS::BOARD_SIZE) { p = 2 * PARAMS::BOARD_SIZE - p; v = -v; }
                next_state.target_position[i] = p;
                next_state.target_velocity[i] = v;
            }
        }
        next_state.step = state.step + 1;
        return PARAMS::DT;
    }
    template <typename DEVICE, typename SPEC, typename ACTION_SPEC, typename RNG>
    RL_TOOLS_FUNCTION_PLACEMENT static typename SPEC::T reward(DEVICE& device, const rl::environments::Chase<SPEC>& env, typename rl::environments::Chase<SPEC>::Parameters& parameters, const typename rl::environments::Chase<SPEC>::State& state, const Matrix<ACTION_SPEC>& action, const typename rl::environments::Chase<SPEC>::State& next_state, RNG& rng) {
        using T = typename SPEC::T;
        using PARAMS = typename SPEC::PARAMETERS;
        const T before = rl::environments::chase::distance(device, state.agent_position, state.target_position);
        const T after = rl::environments::chase::distance(device, next_state.agent_position, next_state.target_position);
        T r = (before - after) / (PARAMS::AGENT_MAX_SPEED * PARAMS::DT);
        if (after < PARAMS::CATCH_DISTANCE) {
            r += PARAMS::CATCH_BONUS;
        }
        return r;
    }
    template <typename DEVICE, typename SPEC, typename OBS_TYPE_SPEC, typename OBS_SPEC, typename RNG>
    RL_TOOLS_FUNCTION_PLACEMENT static void observe(DEVICE& device, const rl::environments::Chase<SPEC>& env, const typename rl::environments::Chase<SPEC>::Parameters& parameters, const typename rl::environments::Chase<SPEC>::State& state, const rl::environments::chase::Observation<OBS_TYPE_SPEC>&, Matrix<OBS_SPEC>& observation, RNG& rng) {
        static_assert(OBS_SPEC::ROWS == 1);
        static_assert(OBS_SPEC::COLS == 6);
        using PARAMS = typename SPEC::PARAMETERS;
        set(observation, 0, 0, (state.target_position[0] - state.agent_position[0]) / PARAMS::BOARD_SIZE);
        set(observation, 0, 1, (state.target_position[1] - state.agent_position[1]) / PARAMS::BOARD_SIZE);
        set(observation, 0, 2, state.agent_velocity[0] / PARAMS::AGENT_MAX_SPEED);
        set(observation, 0, 3, state.agent_velocity[1] / PARAMS::AGENT_MAX_SPEED);
        set(observation, 0, 4, state.target_velocity[0] / PARAMS::TARGET_MAX_SPEED);
        set(observation, 0, 5, state.target_velocity[1] / PARAMS::TARGET_MAX_SPEED);
    }
    template <typename DEVICE, typename SPEC, typename RNG>
    RL_TOOLS_FUNCTION_PLACEMENT static bool terminated(DEVICE& device, const rl::environments::Chase<SPEC>& env, typename rl::environments::Chase<SPEC>::Parameters& parameters, const typename rl::environments::Chase<SPEC>::State state, RNG& rng) {
        return rl::environments::chase::distance(device, state.agent_position, state.target_position) < SPEC::PARAMETERS::CATCH_DISTANCE;
    }
}
RL_TOOLS_NAMESPACE_WRAPPER_END
