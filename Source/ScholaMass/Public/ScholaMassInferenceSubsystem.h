// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "MassSubsystemBase.h"
#include "Mass/ExternalSubsystemTraits.h"
#include "Inference/NPCBackgroundInferenceSubsystem.h"
#include "ScholaMassInferenceBridge.h"
#include "ScholaMassPolicy.h"
#include "ScholaMassInferenceSubsystem.generated.h"

/**
 * @brief Runs background inference for Mass entities in one world.
 *
 * Create a policy per model with CreatePolicy, spawn entities with FScholaMassPolicy::BatchCreateEntities,
 * and run subclasses of UScholaMassGatherProcessor and UScholaMassApplyProcessor each frame.
 */
UCLASS()
class SCHOLAMASS_API UScholaMassInferenceSubsystem : public UMassSubsystemBase
{
	GENERATED_BODY()

public:
	/**
	 * @brief Creates (or joins) the background bucket for a model and sizes its queue from the crowd. Game thread.
	 * @param[in] Config The model, decision rate and crowd size.
	 * @param[out] OutError Why creation failed; empty on success.
	 * @return The policy; invalid on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Mass")
	FScholaMassPolicy CreatePolicy(const FScholaMassPolicyConfig& Config, FString& OutError);

	/** @brief Cumulative counters for a policy. FScholaMassPolicy::GetStats is the usual entry point. */
	UFUNCTION(BlueprintCallable, Category = "Schola|Mass")
	FScholaMassPolicyStats GetPolicyStats(const FScholaMassPolicy& Policy);

	/**
	 * @brief Low-level: acquires a bucket with explicit spaces and scheduler settings. Game thread.
	 *
	 * Settings.DefaultMaxAgeMs is the action age limit. Entities need the schedule, state and bucket fragments.
	 *
	 * @return The bucket id for FScholaInferenceBucketFragment, or INDEX_NONE with OutError set.
	 */
	int32 AcquireBucket(UNPCBackgroundBackendProvider& Provider, const FInteractionDefinition& Definition,
		const FNPCBackgroundSchedulerSettings& Settings, FString& OutError);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	friend class UScholaMassGatherProcessor;
	friend class UScholaMassApplyProcessor;
	friend class UScholaMassScheduleObserver;
	friend class UScholaMassDecideNowProcessor;
	friend struct FScholaMassTestAccess;

	/** @brief Guards Bridge. Fill callbacks may run in parallel and must not enter Bridge. */
	FCriticalSection Mutex;

	/** @brief Transport between Mass entities and the background schedulers. */
	FScholaMassInferenceBridge Bridge;

	UPROPERTY()
	TObjectPtr<UNPCBackgroundInferenceSubsystem> Registry;

	TMap<int32, FNPCBackgroundBucketHandle> Handles;

	/** @brief Providers created by CreatePolicy, kept alive with their model data. */
	UPROPERTY()
	TArray<TObjectPtr<UNPCBackgroundBackendProvider>> PolicyProviders;
};

template <>
struct TMassExternalSubsystemTraits<UScholaMassInferenceSubsystem>
{
	enum
	{
		GameThreadOnly = false,
		ThreadSafeWrite = false
	};
};
