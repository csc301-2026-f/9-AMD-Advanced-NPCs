// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** Native numerical integration module. No learner or gameplay task is created on startup. */
class SCHOLARLTOOLS_API FScholaRLToolsModule : public IModuleInterface
{
public:
    /** Source provenance, from the pinned dependency's REVISION rather than its historical header. */
    const TCHAR* GetRLToolsRevision() const;
};
