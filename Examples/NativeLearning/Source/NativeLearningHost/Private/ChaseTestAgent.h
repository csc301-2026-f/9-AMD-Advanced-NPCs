// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Agent/AgentInterface.h"
#include "Steppers/AdaptiveAgentInterface.h"
#include "ChaseWorld.h"
#include "ChaseTestAgent.generated.h"

/**
 * The chase_sim chaser as a Schola agent: observes ChaseWorld (10 values), acts with a 2-D velocity, and reports
 * the world's reward and episode end through IAdaptiveAgent. Training starts come from the same seed family as
 * chase_sim. Pattern changes apply from the next episode, like chase_sim's `pattern` command between blocks.
 */
UCLASS()
class UChaseTestAgent : public UObject, public IAgent, public IAdaptiveAgent
{
    GENERATED_BODY()
public:
    void Begin(uint64 TrainSeed, online_sac::RunnerPattern InPattern);

    void Define_Implementation(FInteractionDefinition& OutDefinition) override;
    void Act_Implementation(const FInstancedStruct& InAction) override;
    void Observe_Implementation(FInstancedStruct& OutObservations) override;
    void SetStatus_Implementation(EAgentStatus NewStatus) override {}
    EAgentStatus GetStatus_Implementation() override { return EAgentStatus::Running; }

    float ConsumeReward_Implementation() override;
    void GetEpisodeStatus_Implementation(bool& bOutTerminated, bool& bOutTruncated) override;
    void ResetEpisode_Implementation() override;

    /** Used from the next ResetEpisode. */
    online_sac::RunnerPattern Pattern = online_sac::RunnerPattern::RightTurn;
    online_sac::ChaseConfig Config;
    int64 Moves = 0;
    int64 EpisodesStarted = 0;

private:
    online_sac::ChaseWorld World;
    online_sac::ChaseRng Rng;
    float Reward = 0;
    bool bTerminated = false;
    bool bTruncated = false;
};
