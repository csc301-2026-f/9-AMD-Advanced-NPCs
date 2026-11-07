// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Policies/RLToolsPolicySnapshot.h"
#include "Learning/RLToolsPolicyConversion.h"

bool URLToolsPolicySnapshot::Configure(FGuid Session, const FInteractionDefinition& Definition,
    std::shared_ptr<const Schola::RLTools::FPolicy> Policy)
{
    if (!Policy || !InitializeSnapshot(Session, static_cast<int64>(Policy->GetVersion()), Definition)) return false;
    NativePolicy = std::move(Policy);
    return true;
}
bool URLToolsPolicySnapshot::DispatchEvaluation(const TInstancedStruct<FPoint>& Observation, TInstancedStruct<FPoint>& Action) const
{
    using namespace Schola::RLTools;
    FInteractionDefinition Definition;
    std::vector<float> Input, Output;
    return NativePolicy && GetInteractionDefinition(Definition) && ObservationVector(Observation, Definition, Input)
        && NativePolicy->Evaluate(Input, Output) && ActionPoint(Output, Definition, Action);
}
