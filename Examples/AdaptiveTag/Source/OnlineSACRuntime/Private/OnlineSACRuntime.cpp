// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// The only translation unit that includes RLtools: some RLtools headers define non-inline
// functions (e.g. devices::cpu::display_compile_options), so a second including .cpp fails to link.
#include "RLtoolsSetup.h"
#include "ChaseTask.inl"
#include "ChaseSACExperiment.inl"
#include "OnlineSACLearner.inl"
