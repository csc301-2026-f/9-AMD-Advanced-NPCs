// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"

namespace SharedPolicyRouting
{
	constexpr int32 ObservationWidth = 64;
	constexpr int32 ActionWidth = 2;
	constexpr int32 ExpertCount = 2;

	struct FModelCall
	{
		int32 Expert = INDEX_NONE;
		int32 RowCount = 0;
	};

	struct FRequestReport
	{
		TArray<FModelCall> Calls;
	};

	class IExpertBackend
	{
	public:
		virtual ~IExpertBackend() = default;

		virtual bool Run(
			int32 Expert,
			TConstArrayView<float> GroupedObservations,
			int32 RowCount,
			TArray<float>& OutGroupedActions,
			FString& OutError) = 0;
	};

	bool RouteRequest(
		IExpertBackend& Backend,
		TConstArrayView<float> Observations,
		TConstArrayView<int32> Experts,
		TArray<float>& OutActions,
		FRequestReport& OutReport,
		FString& OutError);

	enum class EArchitecture : uint8
	{
		C,
		D,
	};

	class FNneExpertBackend final : public IExpertBackend
	{
	public:
		explicit FNneExpertBackend(EArchitecture InArchitecture);
		~FNneExpertBackend();

		bool Initialize(FString& OutError);

		virtual bool Run(
			int32 Expert,
			TConstArrayView<float> GroupedObservations,
			int32 RowCount,
			TArray<float>& OutGroupedActions,
			FString& OutError) override;

	private:
		struct FImpl;
		TUniquePtr<FImpl> Impl;
	};
}

#endif
