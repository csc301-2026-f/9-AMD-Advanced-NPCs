// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Policies/AdaptivePolicySnapshot.h"
#include <memory>
#include "RLToolsPolicySnapshot.generated.h"

namespace Schola::RLTools { class FPolicy; }

/** Immutable native actor, fixed preprocessing and Schola action conversion, independent of the world. */
UCLASS(BlueprintType)
class SCHOLARLTOOLS_API URLToolsPolicySnapshot : public UAdaptivePolicySnapshot
{
    GENERATED_BODY()
protected:
    bool DispatchEvaluation(const TInstancedStruct<FPoint>& Observation, TInstancedStruct<FPoint>& Action) const override;
private:
    friend class URLToolsWorldSubsystem;
    bool Configure(FGuid Session, const FInteractionDefinition& Definition, std::shared_ptr<const Schola::RLTools::FPolicy> Policy);
    std::shared_ptr<const Schola::RLTools::FPolicy> NativePolicy;
};
