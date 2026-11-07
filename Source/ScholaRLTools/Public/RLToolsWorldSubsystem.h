// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Policies/AdaptiveLearningTypes.h"
#include "RLToolsPolicyTypes.h"
#include <memory>
#include "RLToolsWorldSubsystem.generated.h"

class URLToolsNPCPolicy;
class UAdaptivePolicySnapshot;
struct FInteractionDefinition;
namespace Schola::RLTools { struct FWorldService; }

/** World/PIE-local learner ownership and event-driven, fair background training.
 * At most MaximumConcurrentWorkers pool tasks execute concurrently. Numerical updates are cooperative,
 * not preemptible. Workers never access policy UObjects or gameplay state.
 */
UCLASS(Config = Game)
class SCHOLARLTOOLS_API URLToolsWorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category = "Schola|RLtools", meta = (ClampMin = "1", ClampMax = "8"))
    int32 MaximumConcurrentWorkers = 2;

    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;

    UFUNCTION(BlueprintPure, Category = "Schola|RLtools")
    FRLToolsWorldStats GetExecutionStats() const;

protected:
    bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    friend class URLToolsNPCPolicy;
    friend struct FRLToolsWorldTestAccess;
    bool ValidateBinding(const URLToolsNPCPolicy& Policy, const FInteractionDefinition& Definition, FGuid ExistingBinding, FString& Error) const;
    bool Bind(URLToolsNPCPolicy& Policy, const FInteractionDefinition& Definition, FGuid& Binding, FString& Error);
    void Release(FGuid Binding);
    bool Decide(FGuid Binding, const FAdaptiveDecisionRequest& Request, EAdaptiveInferenceMode Mode, FAdaptiveDecision& Decision, FString& Error);
    EAdaptiveSubmitResult Submit(FGuid Binding, const FAdaptiveExperience& Experience, FString& Error);
    bool Retire(FGuid Binding, int64 Decision, bool bCancelled);
    void Retrieved(FGuid Binding, int64 Decision);
    void EpisodeReset(FGuid Binding, int64 Episode);
    bool LearningControl(FGuid Binding, bool bEnabled, FGuid Operation, FString& Error);
    bool LearningReset(FGuid Binding, int32 Seed, FGuid Operation, FString& Error);
    bool Snapshot(FGuid Binding, UAdaptivePolicySnapshot*& OutSnapshot, FString& Error);
    bool Statistics(FGuid Binding, FRLToolsLearningStats& OutStats) const;
    bool ActiveSettings(FGuid Binding, FRLToolsRuntimeSettings& OutSettings) const;
    void HandleCompletion(FGuid Learner, uint8 Kind, FGuid Binding, FGuid Operation, const FString& Error);

    std::shared_ptr<Schola::RLTools::FWorldService> Runtime;
    bool bActive = false;
};
