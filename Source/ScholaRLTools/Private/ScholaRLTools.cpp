// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ScholaRLTools.h"

const TCHAR* FScholaRLToolsModule::GetRLToolsRevision() const
{
    return TEXT(SCHOLA_RLTOOLS_REVISION);
}

IMPLEMENT_MODULE(FScholaRLToolsModule, ScholaRLTools);
