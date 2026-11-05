// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Keep RLtools operation definitions in this single translation unit as the learner is extracted.
#include "HAL/Platform.h"
#include <rl_tools/operations/cpu_mux.h>
#include <rl_tools/nn/optimizers/adam/instance/operations_generic.h>
#include <rl_tools/nn/operations_cpu_mux.h>
#include <rl_tools/nn/layers/sample_and_squash/operations_generic.h>
#include <rl_tools/nn_models/mlp/operations_generic.h>
#include <rl_tools/nn_models/sequential/operations_generic.h>
#include <rl_tools/nn/optimizers/adam/operations_generic.h>
#include "RLToolsSpaces.h"
#include <rl_tools/rl/algorithms/sac/loop/core/config.h>
#include <rl_tools/rl/algorithms/sac/loop/core/operations_generic.h>
#include <rl_tools/rl/algorithms/sac/operations_generic.h>

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ScholaRLTools.h"

namespace
{
namespace rlt = rl_tools;
using Device = rlt::devices::DEVICE_FACTORY<>;
using TI = Device::index_t;
using TypePolicy = rlt::numeric_types::Policy<float>;

template <typename Environment>
struct BuildParameters : rlt::rl::algorithms::sac::loop::core::DefaultParameters<TypePolicy, TI, Environment>
{
    static constexpr TI STEP_LIMIT = 256;
    static constexpr TI REPLAY_BUFFER_CAP = 256;
    static constexpr TI ACTOR_HIDDEN_DIM = 16;
    static constexpr TI CRITIC_HIDDEN_DIM = 16;
    static constexpr bool SAMPLE_ENVIRONMENT_PARAMETERS = false;
};

// These are test configurations, not a catalogue of supported gameplay profiles.
template <TI Observations, TI Actions>
bool VerifySACBuild()
{
    using Environment = rlt::rl::environments::ScholaSpaces<float, TI, Observations, Actions>;
    using Rng = Device::SPEC::RANDOM::ENGINE<>;
    using Config = rlt::rl::algorithms::sac::loop::core::Config<TypePolicy, TI, Rng, Environment, BuildParameters<Environment>>;
    using State = typename Config::template State<Config>;
    using Actor = typename Config::EVAL_ACTOR_TYPE::template CHANGE_CAPABILITY<rlt::nn::capability::Forward<true>>;
    Device DeviceInstance;
    auto StateInstance = MakeUnique<State>();
    rlt::malloc(DeviceInstance, *StateInstance);
    rlt::init(DeviceInstance, *StateInstance, 1);
    Actor Snapshot;
    typename Actor::template Buffer<true> Buffer;
    rlt::malloc(DeviceInstance, Snapshot);
    rlt::malloc(DeviceInstance, Buffer);
    rlt::copy(DeviceInstance, DeviceInstance, StateInstance->actor_critic.actor, Snapshot);
    rlt::Tensor<rlt::tensor::Specification<float, TI, rlt::tensor::Shape<TI, 1, 1, Observations>, false>> Input;
    rlt::Tensor<rlt::tensor::Specification<float, TI, rlt::tensor::Shape<TI, 1, 1, Actions>, false>> Output;
    for (TI Index = 0; Index < Observations; ++Index) rlt::set(DeviceInstance, Input, 0.0f, 0, 0, Index);
    rlt::evaluate(DeviceInstance, Snapshot, Input, Output, Buffer, StateInstance->rng, rlt::Mode<rlt::mode::Evaluation<>>{});
    bool bValid = true;
    for (TI Index = 0; Index < Actions; ++Index)
    {
        const float Value = rlt::get(DeviceInstance, Output, 0, 0, Index);
        bValid = bValid && FMath::IsFinite(Value) && Value >= -1.0f && Value <= 1.0f;
    }
    rlt::free(DeviceInstance, Buffer);
    rlt::free(DeviceInstance, Snapshot);
    rlt::free(DeviceInstance, *StateInstance);
    return bValid;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsNativeBuildTest, "Schola.RLTools.NativeBuild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRLToolsNativeBuildTest::RunTest(const FString& Parameters)
{
    auto& Module = FModuleManager::LoadModuleChecked<FScholaRLToolsModule>(TEXT("ScholaRLTools"));
    TestEqual(TEXT("Runtime module records the vendored revision"), FString(Module.GetRLToolsRevision()),
        FString(TEXT("b32d9985c65a5e098a6bbf190fd994962d288b99")));
    TestTrue(TEXT("CPU SAC allocates, initializes and evaluates a 3 observation / 2 action configuration"), VerifySACBuild<3, 2>());
    TestTrue(TEXT("CPU SAC allocates, initializes and evaluates a 5 observation / 1 action configuration"), VerifySACBuild<5, 1>());
    return true;
}
#endif
