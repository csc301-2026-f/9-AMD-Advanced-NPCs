// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "RLToolsConfigurationAsset.h"
#include "Policies/RLToolsNPCPolicy.h"
#include "Spaces/BoxSpace.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#if WITH_EDITOR
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#endif

FString URLToolsConfigurationAsset::GetConfigurationId() const
{
    const TCHAR* Prefix = BuildSettings.Algorithm == ERLToolsAlgorithm::SAC ? TEXT("sac")
        : BuildSettings.Algorithm == ERLToolsAlgorithm::TD3 ? TEXT("td3") : TEXT("ppo");
    return FString::Printf(TEXT("%s/project/%s"), Prefix, *BuildSettings.Name);
}
bool URLToolsConfigurationAsset::DescribeRequested(FRLToolsConfiguration& R, FString& Error) const
{
    const auto& S = BuildSettings;
    auto Fail = [&Error](const TCHAR* Message) { Error = Message; return false; };
    if (S.Name.IsEmpty() || S.Name.Len() > 48 || S.Name[0] < 'a' || S.Name[0] > 'z')
        return Fail(TEXT("Configuration name must start with a lowercase ASCII letter and contain 1..48 letters/digits/hyphens."));
    for (TCHAR C : S.Name)
        if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '-'))
            return Fail(TEXT("Configuration name permits only lowercase ASCII letters, digits and hyphens."));
    if (S.Algorithm != ERLToolsAlgorithm::SAC && S.Algorithm != ERLToolsAlgorithm::TD3 && S.Algorithm != ERLToolsAlgorithm::PPO)
        return Fail(TEXT("Choose SAC, TD3 or PPO."));
    if (!UAdaptiveNPCPolicy::ValidateBoxSpaceDefinition(InteractionDefinition.ObsSpaceDefn, false, Error)
        || !UAdaptiveNPCPolicy::ValidateBoxSpaceDefinition(InteractionDefinition.ActionSpaceDefn, true, Error)) return false;
    if (S.ActorHiddenWidth <= 0 || S.CriticHiddenWidth <= 0 || S.ActorLayers < 3 || S.ActorLayers > 16
        || S.CriticLayers < 3 || S.CriticLayers > 16 || S.BatchSize < 2)
        return Fail(TEXT("Hidden widths must be positive, layer counts 3..16 (including output), and batch size at least two."));
    auto Activation = [](ERLToolsActivation Value) { return Value == ERLToolsActivation::ReLU || Value == ERLToolsActivation::Tanh; };
    if (!Activation(S.ActorActivation) || !Activation(S.CriticActivation))
        return Fail(TEXT("Supported hidden activations are ReLU and Tanh."));
    auto Unit = [](float V) { return FMath::IsFinite(V) && V > 0 && V <= 1; };
    auto Positive = [](float V) { return FMath::IsFinite(V) && V > 0; };
    auto Nonnegative = [](float V) { return FMath::IsFinite(V) && V >= 0; };
    if (!Unit(S.Discount)) return Fail(TEXT("Discount must be finite and in (0,1]."));
    if (S.Algorithm == ERLToolsAlgorithm::PPO)
    {
        if (S.RolloutLength < S.BatchSize || S.RolloutLength % S.BatchSize != 0)
            return Fail(TEXT("PPO rollout length must be at least the batch size and divisible by it."));
        if (!Positive(S.PPOClip) || !Nonnegative(S.GAE) || S.GAE > 1 || !Nonnegative(S.PPOEntropyCoefficient) || !Positive(S.InitialActionStd))
            return Fail(TEXT("Invalid PPO clipping, advantage, entropy or action-distribution parameters."));
    }
    else
    {
        if (S.ReplayCapacity < S.BatchSize || !Unit(S.TargetTau) || S.ActorInterval < 0 || S.TargetInterval < 0)
            return Fail(TEXT("Replay capacity must cover a batch, target tau must be in (0,1], and update intervals nonnegative."));
        if (S.Algorithm == ERLToolsAlgorithm::SAC && !Positive(S.EntropyCoefficient))
            return Fail(TEXT("SAC entropy coefficient must be finite and positive."));
        if (S.Algorithm == ERLToolsAlgorithm::TD3 && (!Nonnegative(S.TargetActionNoise) || !Nonnegative(S.TargetActionNoiseClip)))
            return Fail(TEXT("TD3 target smoothing parameters must be finite and nonnegative."));
    }
    R.Id = GetConfigurationId();
    R.Algorithm = S.Algorithm;
    R.Observations = InteractionDefinition.ObsSpaceDefn.Get<FBoxSpace>().Dimensions.Num();
    R.Actions = InteractionDefinition.ActionSpaceDefn.Get<FBoxSpace>().Dimensions.Num();
    R.HiddenWidth = S.ActorHiddenWidth;
    R.CriticHiddenWidth = S.CriticHiddenWidth;
    R.ActorLayers = S.ActorLayers; R.CriticLayers = S.CriticLayers;
    R.ActorActivation = S.ActorActivation; R.CriticActivation = S.CriticActivation;
    R.BatchSize = S.BatchSize;
    R.ReplayCapacity = S.Algorithm == ERLToolsAlgorithm::PPO ? 0 : S.ReplayCapacity;
    R.RolloutLength = S.Algorithm == ERLToolsAlgorithm::PPO ? S.RolloutLength : 0;
    R.Discount = S.Discount;
    const int64 Columns = 2LL * R.Observations + R.Actions + 4;
    const int64 Rows = S.Algorithm == ERLToolsAlgorithm::PPO ? S.RolloutLength : S.ReplayCapacity;
    if (Columns > MAX_int32 || Rows > MAX_int32 / Columns)
        return Fail(TEXT("Experience storage dimensions overflow the supported 32-bit index range."));
    for (const auto& Pair : {TPair<int64, int64>{R.Observations, R.HiddenWidth},
        {R.HiddenWidth, S.Algorithm == ERLToolsAlgorithm::SAC ? 2LL * R.Actions : R.Actions},
        {int64(R.Observations) + R.Actions, R.CriticHiddenWidth}, {R.HiddenWidth, R.HiddenWidth},
        {R.CriticHiddenWidth, R.CriticHiddenWidth}, {R.BatchSize, FMath::Max<int64>(FMath::Max(R.HiddenWidth, R.CriticHiddenWidth),
            FMath::Max<int64>(int64(R.Observations) + R.Actions, S.Algorithm == ERLToolsAlgorithm::SAC ? 2LL * R.Actions : R.Actions))}})
        if (Pair.Key * Pair.Value > MAX_int32) return Fail(TEXT("Network dimensions overflow the supported 32-bit index range."));
    if (S.Algorithm != ERLToolsAlgorithm::PPO)
    {
        R.TargetTau = S.TargetTau;
        const int32 DefaultInterval = S.Algorithm == ERLToolsAlgorithm::TD3 ? 2 : 1;
        R.ActorInterval = S.ActorInterval ? S.ActorInterval : DefaultInterval;
        R.TargetInterval = S.TargetInterval ? S.TargetInterval : DefaultInterval;
    }
    R.EntropyCoefficient = S.EntropyCoefficient; R.bAdaptiveEntropy = S.bAdaptiveEntropy;
    R.TargetActionNoise = S.TargetActionNoise; R.TargetActionNoiseClip = S.TargetActionNoiseClip;
    R.PPOClip = S.PPOClip; R.GAE = S.GAE; R.PPOEntropyCoefficient = S.PPOEntropyCoefficient;
    R.InitialActionStd = S.InitialActionStd; R.bNormalizeAdvantage = S.bNormalizeAdvantage; R.bLearnActionStd = S.bLearnActionStd;
    Error.Reset();
    return true;
}
bool URLToolsConfigurationAsset::ValidateConfiguration(FString& Diagnostic) const
{
    FRLToolsConfiguration Requested;
    return DescribeRequested(Requested, Diagnostic);
}
bool URLToolsConfigurationAsset::GetCompiledConfiguration(FRLToolsConfiguration& Out, FString& Error) const
{
    FRLToolsConfiguration Want;
    if (!DescribeRequested(Want, Error)) return false;
    for (const auto& Have : URLToolsNPCPolicy::GetAvailableConfigurations(Want.Algorithm))
    {
        if (Have.Id != Want.Id) continue;
        bool Same = Have.Observations == Want.Observations && Have.Actions == Want.Actions
            && Have.HiddenWidth == Want.HiddenWidth && Have.CriticHiddenWidth == Want.CriticHiddenWidth
            && Have.ActorLayers == Want.ActorLayers && Have.CriticLayers == Want.CriticLayers
            && Have.ActorActivation == Want.ActorActivation && Have.CriticActivation == Want.CriticActivation
            && Have.BatchSize == Want.BatchSize && Have.ReplayCapacity == Want.ReplayCapacity && Have.RolloutLength == Want.RolloutLength
            && Have.Discount == Want.Discount;
        if (Want.Algorithm != ERLToolsAlgorithm::PPO)
            Same = Same && Have.TargetTau == Want.TargetTau && Have.ActorInterval == Want.ActorInterval && Have.TargetInterval == Want.TargetInterval;
        if (Want.Algorithm == ERLToolsAlgorithm::SAC)
            Same = Same && Have.EntropyCoefficient == Want.EntropyCoefficient && Have.bAdaptiveEntropy == Want.bAdaptiveEntropy;
        if (Want.Algorithm == ERLToolsAlgorithm::TD3)
            Same = Same && Have.TargetActionNoise == Want.TargetActionNoise && Have.TargetActionNoiseClip == Want.TargetActionNoiseClip;
        if (Want.Algorithm == ERLToolsAlgorithm::PPO)
            Same = Same && Have.PPOClip == Want.PPOClip && Have.GAE == Want.GAE && Have.PPOEntropyCoefficient == Want.PPOEntropyCoefficient
                && Have.InitialActionStd == Want.InitialActionStd && Have.bNormalizeAdvantage == Want.bNormalizeAdvantage && Have.bLearnActionStd == Want.bLearnActionStd;
        if (!Same)
        {
            Error = TEXT("Compiled RLtools configuration is stale. Generate Project Configuration, stop PIE, close the editor and rebuild the project.");
            return false;
        }
        Out = Have;
        Error.Reset();
        return true;
    }
    Error = TEXT("RLtools configuration is not compiled into this project. Generate Project Configuration, close the editor and rebuild. Packaged games cannot compile new configurations.");
    return false;
}

bool URLToolsConfigurationAsset::WriteRequest(const FString& Manifest, const FString& Source, bool bRemove, FString& Error) const
{
#if WITH_EDITOR
    FRLToolsConfiguration Requested;
    if (!bRemove && !DescribeRequested(Requested, Error)) return false;
    if (Source.IsEmpty() || Source.Len() > 2048)
    { Error = TEXT("Configuration request requires a nonempty source identity of at most 2048 characters."); return false; }
    const TCHAR* Algorithm = BuildSettings.Algorithm == ERLToolsAlgorithm::SAC ? TEXT("SAC") : BuildSettings.Algorithm == ERLToolsAlgorithm::TD3 ? TEXT("TD3") : TEXT("PPO");
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    FString Previous;
    TArray<TSharedPtr<FJsonValue>> Entries;
    if (IFileManager::Get().FileExists(*Manifest))
    {
        if (IFileManager::Get().FileSize(*Manifest) > 4 * 1024 * 1024)
        { Error = TEXT("Existing ScholaRLTools.json exceeds 4 MiB; it was not overwritten."); return false; }
        if (!FFileHelper::LoadFileToString(Previous, *Manifest)
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Previous), Root) || !Root.IsValid())
        { Error = TEXT("Existing ScholaRLTools.json is malformed; it was not overwritten."); return false; }
        double Version = 0;
        const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
        if (Root->Values.Num() != 2 || !Root->TryGetNumberField(TEXT("schemaVersion"), Version) || Version != 1
            || !Root->TryGetArrayField(TEXT("profiles"), Existing))
        { Error = TEXT("Existing ScholaRLTools.json must contain schemaVersion 1 and a profiles array; it was not overwritten."); return false; }
        Entries = *Existing;
    }
    int32 Match = INDEX_NONE;
    for (int32 I = 0; I < Entries.Num(); ++I)
    {
        if (!Entries[I].IsValid() || Entries[I]->Type != EJson::Object)
        { Error = TEXT("Existing profile entry is not an object; request was not modified."); return false; }
        const auto Entry = Entries[I]->AsObject();
        FString Name, Kind, Owner;
        if (!Entry->TryGetStringField(TEXT("name"), Name) || !Entry->TryGetStringField(TEXT("algorithm"), Kind))
        { Error = TEXT("Existing profile is missing name/algorithm; request was not modified."); return false; }
        Entry->TryGetStringField(TEXT("source"), Owner);
        if (!bRemove && Name == BuildSettings.Name && Kind == Algorithm && Owner != Source)
        { Error = TEXT("Configuration name is already owned by another request. Choose a different name."); return false; }
        if (Owner == Source)
        {
            if (Match != INDEX_NONE)
            { Error = TEXT("Configuration name is already owned by another request (or duplicated). Choose a different name."); return false; }
            Match = I;
        }
    }
    if (bRemove)
    {
        if (Match == INDEX_NONE) { Error = TEXT("No request owned by this asset needs removal."); return true; }
        Entries.RemoveAt(Match);
    }
    else
    {
        if (Match == INDEX_NONE && Entries.Num() >= 64)
        { Error = TEXT("At most 64 project configurations may be generated per build."); return false; }
        auto Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), BuildSettings.Name);
        Entry->SetStringField(TEXT("source"), Source);
        Entry->SetStringField(TEXT("algorithm"), Algorithm);
        Entry->SetNumberField(TEXT("observations"), Requested.Observations);
        Entry->SetNumberField(TEXT("actions"), Requested.Actions);
        auto Network = MakeShared<FJsonObject>();
        Network->SetNumberField(TEXT("actorHiddenWidth"), BuildSettings.ActorHiddenWidth);
        Network->SetNumberField(TEXT("criticHiddenWidth"), BuildSettings.CriticHiddenWidth);
        Network->SetNumberField(TEXT("actorLayers"), BuildSettings.ActorLayers);
        Network->SetNumberField(TEXT("criticLayers"), BuildSettings.CriticLayers);
        Network->SetStringField(TEXT("actorActivation"), BuildSettings.ActorActivation == ERLToolsActivation::Tanh ? TEXT("Tanh") : TEXT("ReLU"));
        Network->SetStringField(TEXT("criticActivation"), BuildSettings.CriticActivation == ERLToolsActivation::Tanh ? TEXT("Tanh") : TEXT("ReLU"));
        Entry->SetObjectField(TEXT("network"), Network);
        Entry->SetNumberField(TEXT("batchSize"), BuildSettings.BatchSize);
        Entry->SetNumberField(BuildSettings.Algorithm == ERLToolsAlgorithm::PPO ? TEXT("rolloutLength") : TEXT("replayCapacity"),
            BuildSettings.Algorithm == ERLToolsAlgorithm::PPO ? BuildSettings.RolloutLength : BuildSettings.ReplayCapacity);
        auto Parameters = MakeShared<FJsonObject>();
        Parameters->SetNumberField(TEXT("discount"), BuildSettings.Discount);
        if (BuildSettings.Algorithm != ERLToolsAlgorithm::PPO)
        {
            Parameters->SetNumberField(TEXT("targetTau"), BuildSettings.TargetTau);
            Parameters->SetNumberField(TEXT("actorInterval"), BuildSettings.ActorInterval);
            Parameters->SetNumberField(TEXT("targetInterval"), BuildSettings.TargetInterval);
        }
        if (BuildSettings.Algorithm == ERLToolsAlgorithm::SAC)
        {
            Parameters->SetNumberField(TEXT("entropyCoefficient"), BuildSettings.EntropyCoefficient);
            Parameters->SetBoolField(TEXT("adaptiveEntropy"), BuildSettings.bAdaptiveEntropy);
        }
        else if (BuildSettings.Algorithm == ERLToolsAlgorithm::TD3)
        {
            Parameters->SetNumberField(TEXT("targetActionNoise"), BuildSettings.TargetActionNoise);
            Parameters->SetNumberField(TEXT("targetActionNoiseClip"), BuildSettings.TargetActionNoiseClip);
        }
        else
        {
            Parameters->SetNumberField(TEXT("ppoClip"), BuildSettings.PPOClip);
            Parameters->SetNumberField(TEXT("gae"), BuildSettings.GAE);
            Parameters->SetNumberField(TEXT("ppoEntropyCoefficient"), BuildSettings.PPOEntropyCoefficient);
            Parameters->SetNumberField(TEXT("initialActionStd"), BuildSettings.InitialActionStd);
            Parameters->SetBoolField(TEXT("normalizeAdvantage"), BuildSettings.bNormalizeAdvantage);
            Parameters->SetBoolField(TEXT("learnActionStd"), BuildSettings.bLearnActionStd);
        }
        Entry->SetObjectField(TEXT("parameters"), Parameters);
        if (Match == INDEX_NONE) Entries.Add(MakeShared<FJsonValueObject>(Entry));
        else Entries[Match] = MakeShared<FJsonValueObject>(Entry);
    }
    Root->SetNumberField(TEXT("schemaVersion"), 1);
    Root->SetArrayField(TEXT("profiles"), Entries);
    FString Content;
    if (!FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Content)))
    { Error = TEXT("Could not serialize the project configuration request."); return false; }
    if (Content != Previous)
    {
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Manifest), true);
        const FString Temporary = Manifest + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
        if (!FFileHelper::SaveStringToFile(Content, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            || !IFileManager::Get().Move(*Manifest, *Temporary, true, false, false, true))
        {
            IFileManager::Get().Delete(*Temporary);
            Error = TEXT("Could not save ScholaRLTools.json; check write permissions/source-control checkout and try again.");
            return false;
        }
    }
    Error = bRemove ? TEXT("Request removed. Close the editor and rebuild to remove its compiled configuration.")
        : TEXT("Request generated. Stop PIE, close the editor and rebuild; then assign this asset to the native policy.");
    return true;
#else
    Error = TEXT("Configuration generation/removal is editor-only. Packaged games use configurations compiled during the project build.");
    return false;
#endif
}
void URLToolsConfigurationAsset::GenerateProjectConfiguration()
{
#if WITH_EDITOR
    if (!IsAsset() || HasAnyFlags(RF_Transient) || GetOutermost() == GetTransientPackage())
    { GenerationStatus = TEXT("Save this configuration as a project asset before generating."); return; }
#endif
    WriteRequest(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("ScholaRLTools.json")), GetPathName(), false, GenerationStatus);
}
void URLToolsConfigurationAsset::RemoveProjectConfiguration()
{
    WriteRequest(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("ScholaRLTools.json")), GetPathName(), true, GenerationStatus);
}
