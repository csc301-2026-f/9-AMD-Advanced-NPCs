#include "Steppers/EventStepper.h"
#include "LogScholaInferenceUtils.h"

bool UEventStepper::Init(const TArray<TScriptInterface<IAgent>>& InAgents, const TScriptInterface<IPolicy>& InPolicy){
    Agents = InAgents;
    Policy = InPolicy;

    if(!Agents.Num()){
        UE_LOGFMT(LogScholaInferenceUtils, Error, "EventStepper: No agents provided!");
        return false;
    }

    if (!Policy){
        UE_LOGFMT(LogScholaInferenceUtils, Error, "EventStepper: No policy provided!");
        return false;
    }

    UE_LOGFMT(LogScholaInferenceUtils, Log,"EventStepper initialized with {0} agents", Agents.Num());
    return true;
}

void UEventStepper::TriggerInference()
{
    if (Agents.Num() == 0 || !Policy){UE_LOGFMT(LogScholaInferenceUtils, Error,"EventStepper: Not initialized");
        return;
    }

    UE_LOGFMT(LogScholaInferenceUtils, Log, "EventStepper: Inference triggered");

    TArray<TInstancedStruct<FPoint>> Observations;
    TArray<TInstancedStruct<FPoint>> Actions;

    for (int i = 0; i < Agents.Num(); ++i){
        TInstancedStruct<FPoint> Observation;

        IAgent::Execute_Observe(
            Agents[i].GetObject(),
            Observation
        );

        Observations.Add(MoveTemp(Observation));
    }

    if(!Policy->BatchedThink(Observations, Actions)){
        UE_LOGFMT(LogScholaInferenceUtils, Error, "EventStepper: Inference failed");
        return;
    }

    if(Actions.Num() != Agents.Num()){
        UE_LOGFMT(LogScholaInferenceUtils, Error, "EventStepper: Action count mismatch");
        return;
    }

    for(int i = 0; i < Agents.Num(); i++){
        IAgent::Execute_Act(Agents[i].GetObject(), Actions[i]);
    }

    UE_LOGFMT(LogScholaInferenceUtils, Log, "EventStepper: Actions applied");
}