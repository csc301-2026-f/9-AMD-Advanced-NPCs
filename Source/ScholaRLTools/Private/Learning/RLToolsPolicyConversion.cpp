// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Learning/RLToolsPolicyConversion.h"
#include "Spaces/BoxSpace.h"

namespace Schola::RLTools
{
FSettings ToNative(const FRLToolsRuntimeSettings& S)
{
    FSettings N;
    N.Seed = S.Seed;
    N.QueueCapacity = S.QueueCapacity;
    N.MaxRecordsPerBurst = S.MaxRecordsPerBurst;
    N.LearningStarts = S.LearningStarts;
    N.InitialRandomSteps = S.InitialRandomSteps;
    N.TrainFrequency = S.TrainFrequency;
    N.GradientSteps = S.GradientSteps;
    N.MaxUpdatesPerBurst = S.MaxUpdatesPerBurst;
    N.PublishEveryUpdates = S.PublishEveryUpdates;
    N.PPOEpochs = S.PPOEpochs;
    N.LearningRate = S.LearningRate;
    N.ExplorationNoise = S.ExplorationNoise;
    if (!S.ObservationMean.IsEmpty())
        N.ObservationMean.assign(S.ObservationMean.GetData(), S.ObservationMean.GetData() + S.ObservationMean.Num());
    if (!S.ObservationStd.IsEmpty())
        N.ObservationStd.assign(S.ObservationStd.GetData(), S.ObservationStd.GetData() + S.ObservationStd.Num());
    return N;
}
FRLToolsConfiguration DescribeProfile(const FProfile& P)
{
    FRLToolsConfiguration R;
    R.Id = UTF8_TO_TCHAR(P.Id.c_str());
    R.Algorithm = static_cast<ERLToolsAlgorithm>(P.Algorithm);
    R.Observations = P.Observations;
    R.Actions = P.Actions;
    R.HiddenWidth = P.HiddenWidth;
    R.CriticHiddenWidth = P.CriticHiddenWidth;
    R.ActorActivation = static_cast<ERLToolsActivation>(P.ActorActivation);
    R.CriticActivation = static_cast<ERLToolsActivation>(P.CriticActivation);
    R.ActorLayers = P.ActorLayers;
    R.CriticLayers = P.CriticLayers;
    R.BatchSize = P.BatchSize;
    R.ReplayCapacity = P.ReplayCapacity;
    R.RolloutLength = P.RolloutLength;
    R.Discount = P.Discount;
    R.TargetTau = P.TargetTau;
    R.ActorInterval = P.ActorInterval;
    R.TargetInterval = P.TargetInterval;
    R.EntropyCoefficient = P.EntropyCoefficient;
    R.bAdaptiveEntropy = P.AdaptiveEntropy;
    R.TargetActionNoise = P.TargetActionNoise;
    R.TargetActionNoiseClip = P.TargetActionNoiseClip;
    R.PPOClip = P.PPOClip;
    R.GAE = P.GAE;
    R.PPOEntropyCoefficient = P.PPOEntropyCoefficient;
    R.InitialActionStd = P.InitialActionStd;
    R.bNormalizeAdvantage = P.NormalizeAdvantage;
    R.bLearnActionStd = P.LearnActionStd;
    R.bRecurrent = P.Recurrent;
    R.Device = UTF8_TO_TCHAR(P.Device);
    R.Revision = UTF8_TO_TCHAR(P.Revision);
    return R;
}
bool ValidateConfiguration(ERLToolsAlgorithm Algorithm, const FString& Id, const FRLToolsRuntimeSettings& S,
    const FInteractionDefinition& Definition, FString& Error)
{
    if (S.Seed < 0 || S.QueueCapacity <= 0 || S.MaxRecordsPerBurst <= 0 || S.LearningStarts < 0
        || S.InitialRandomSteps < 0 || S.TrainFrequency <= 0 || S.GradientSteps <= 0
        || S.MaxUpdatesPerBurst <= 0 || S.PublishEveryUpdates <= 0 || S.PPOEpochs <= 0)
    {
        Error = TEXT("Seed, warm-up and initial exploration must be nonnegative; queue and scheduling limits must be positive.");
        return false;
    }
    const std::string NativeId(TCHAR_TO_UTF8(*Id));
    std::string NativeError;
    if (!FLearner::ValidateSettings(NativeId, ToNative(S), NativeError))
    {
        Error = UTF8_TO_TCHAR(NativeError.c_str());
        return false;
    }
    for (const auto& Profile : FLearner::GetProfiles())
    {
        if (Profile.Id != NativeId) continue;
        if (static_cast<ERLToolsAlgorithm>(Profile.Algorithm) != Algorithm)
        {
            Error = TEXT("Algorithm does not match the compiled configuration identifier. Query GetAvailableConfigurations for that algorithm.");
            return false;
        }
        if (Definition.ObsSpaceDefn.Get<FBoxSpace>().Dimensions.Num() != Profile.Observations
            || Definition.ActionSpaceDefn.Get<FBoxSpace>().Dimensions.Num() != Profile.Actions)
        {
            Error = FString::Printf(TEXT("Configuration %s requires %u observations and %u actions. Select an available matching configuration; arbitrary generation is separate work."),
                *Id, Profile.Observations, Profile.Actions);
            return false;
        }
        return true;
    }
    Error = TEXT("Compiled configuration not found.");
    return false;
}
bool SameSettings(const FRLToolsRuntimeSettings& A, const FRLToolsRuntimeSettings& B)
{
    return A.Seed == B.Seed
        && A.QueueCapacity == B.QueueCapacity
        && A.MaxRecordsPerBurst == B.MaxRecordsPerBurst
        && A.LearningStarts == B.LearningStarts
        && A.InitialRandomSteps == B.InitialRandomSteps
        && A.TrainFrequency == B.TrainFrequency
        && A.GradientSteps == B.GradientSteps
        && A.MaxUpdatesPerBurst == B.MaxUpdatesPerBurst
        && A.PublishEveryUpdates == B.PublishEveryUpdates
        && A.PPOEpochs == B.PPOEpochs
        && A.LearningRate == B.LearningRate
        && A.ExplorationNoise == B.ExplorationNoise
        && A.ObservationMean == B.ObservationMean
        && A.ObservationStd == B.ObservationStd;
}
static bool SameBox(const FBoxSpace& A, const FBoxSpace& B)
{
    if (A.Shape != B.Shape || A.Dimensions.Num() != B.Dimensions.Num()) return false;
    for (int32 I = 0; I < A.Dimensions.Num(); ++I)
    {
        const auto& X = A.Dimensions[I];
        const auto& Y = B.Dimensions[I];
        if (X.bHasLow != Y.bHasLow || X.bHasHigh != Y.bHasHigh
            || (X.bHasLow && X.Low != Y.Low) || (X.bHasHigh && X.High != Y.High)) return false;
    }
    return true;
}
bool SameDefinition(const FInteractionDefinition& A, const FInteractionDefinition& B)
{
    return A.AgentType == B.AgentType
        && SameBox(A.ObsSpaceDefn.Get<FBoxSpace>(), B.ObsSpaceDefn.Get<FBoxSpace>())
        && SameBox(A.ActionSpaceDefn.Get<FBoxSpace>(), B.ActionSpaceDefn.Get<FBoxSpace>());
}
bool ObservationVector(const TInstancedStruct<FPoint>& Point, const FInteractionDefinition& Definition, std::vector<float>& Out)
{
    if (Point.GetScriptStruct() != FBoxPoint::StaticStruct()) return false;
    const auto& P = Point.Get<FBoxPoint>();
    const auto& Space = Definition.ObsSpaceDefn.Get<FBoxSpace>();
    const auto Shape = Space.Shape.IsEmpty() ? TArray<int>{Space.Dimensions.Num()} : Space.Shape;
    if ((!P.Shape.IsEmpty() && P.Shape != Shape) || Space.Validate(Point) != ESpaceValidationResult::Success) return false;
    for (float Value : P.Values) if (!FMath::IsFinite(Value)) return false;
    Out.assign(P.Values.GetData(), P.Values.GetData() + P.Values.Num());
    return true;
}
bool ActionPoint(const std::vector<float>& Normalized, const FInteractionDefinition& Definition, TInstancedStruct<FPoint>& Out)
{
    const auto& Space = Definition.ActionSpaceDefn.Get<FBoxSpace>();
    if (Normalized.size() != Space.Dimensions.Num()) return false;
    TArray<float> Values;
    Values.Reserve(Space.Dimensions.Num());
    for (int32 I = 0; I < Space.Dimensions.Num(); ++I)
    {
        if (!FMath::IsFinite(Normalized[I]) || Normalized[I] < -1 || Normalized[I] > 1) return false;
        const auto& D = Space.Dimensions[I];
        const double Value = double(D.Low) + (double(Normalized[I]) + 1.0) * 0.5 * (double(D.High) - D.Low);
        Values.Add(FMath::Clamp(static_cast<float>(Value), D.Low, D.High));
    }
    Out.InitializeAs<FBoxPoint>(Values, Space.Shape.IsEmpty() ? TArray<int>{Space.Dimensions.Num()} : Space.Shape);
    return true;
}
std::vector<float> NormalizeAction(const TInstancedStruct<FPoint>& Point, const FInteractionDefinition& Definition)
{
    const auto& Space = Definition.ActionSpaceDefn.Get<FBoxSpace>();
    const auto& Values = Point.Get<FBoxPoint>().Values;
    std::vector<float> Normalized;
    for (int32 I = 0; I < Values.Num(); ++I)
    {
        const auto& D = Space.Dimensions[I];
        Normalized.push_back(static_cast<float>(FMath::Clamp(2.0 * (double(Values[I]) - D.Low) / (double(D.High) - D.Low) - 1.0, -1.0, 1.0)));
    }
    return Normalized;
}
}
