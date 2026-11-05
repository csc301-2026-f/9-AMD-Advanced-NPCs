// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

namespace Schola::RLTools
{
/** RLtools encodes these algorithm choices in template types. Generated configurations
 * derive from this struct to change them; FSettings contains the runtime choices.
 * Keep this independent of RLtools so profile discovery exposes the effective values.
 */
struct FAlgorithmParameters
{
    static constexpr float Discount = 0.99f;
    static constexpr float TargetTau = 0.005f;
    static constexpr unsigned ActorInterval = 0; // 0 selects SAC=1, TD3=2.
    static constexpr unsigned TargetInterval = 0; // 0 selects SAC=1, TD3=2.
    static constexpr float EntropyCoefficient = 0.5f;
    static constexpr bool AdaptiveEntropy = true;
    static constexpr float TargetActionNoise = 0.2f;
    static constexpr float TargetActionNoiseClip = 0.5f;
    static constexpr float PPOClip = 0.2f;
    static constexpr float GAE = 0.95f;
    static constexpr float PPOEntropyCoefficient = 0.01f;
    static constexpr bool NormalizeAdvantage = true;
    static constexpr float InitialActionStd = 0.5f;
    static constexpr bool LearnActionStd = true;
};
}
