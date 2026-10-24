
#pragma once

#include "CoreMinimal.h"
#include "Steppers/StepperInterface.h"
#include "Agent/AgentInterface.h"
#include "Policies/PolicyInterface.h"
#include "EventStepper.generated.h"

UCLASS(BlueprintType, Blueprintable)
class SCHOLAINFERENCEUTILS_API UEventStepper : public UObject, public IStepper{
    GENERATED_BODY()

public:

    UFUNCTION(BlueprintCallable, Category = "Schola|Stepper")
    bool Init(const TArray<TScriptInterface<IAgent>>& InAgents, const TScriptInterface<IPolicy>& InPolicy) override;

    UFUNCTION(BlueprintCallable, Category = "Schola|Stepper")
    void TriggerInference();

private:

    UPROPERTY()
    TArray<TScriptInterface<IAgent>> Agents;

    UPROPERTY()
    TScriptInterface<IPolicy> Policy;
};