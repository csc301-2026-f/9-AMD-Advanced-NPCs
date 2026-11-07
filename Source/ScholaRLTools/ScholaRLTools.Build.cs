// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
using System;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Security.Cryptography;
using System.Globalization;
using System.Collections.Generic;
using System.Linq;
#if !SCHOLA_GENERATOR_TESTS
using UnrealBuildTool;

public class ScholaRLTools : ModuleRules
{
    public ScholaRLTools(ReadOnlyTargetRules Target) : base(Target)
    {
        // RLtools operation definitions must not be merged into unrelated translation units.
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        bEnableExceptions = false;
        bUseRTTI = false;
        CppStandard = CppStandardVersion.Cpp20;
        FPSemantics = FPSemanticsMode.Precise;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Schola", "ScholaInferenceUtils" });
        PrivateDependencyModuleNames.Add("RLTools");
        PrivateDependencyModuleNames.Add("Json");
        string RevisionFile = Path.GetFullPath(Path.Combine(ModuleDirectory, "../ThirdParty/RLTools/REVISION"));
        PrivateDefinitions.Add("SCHOLA_RLTOOLS_REVISION=\"" + File.ReadAllText(RevisionFile).Trim() + "\"");
        ExternalDependencies.Add(RevisionFile);
        if (Target.ProjectFile != null)
        {
            string ProjectDirectory = Target.ProjectFile.Directory.FullName;
            string Manifest = Path.Combine(ProjectDirectory, "Config/ScholaRLTools.json");
            // Track the path even when absent, so creating/removing the request invalidates the makefile.
            ExternalDependencies.Add(Manifest);
            try
            {
                string Generated = ScholaRLToolsGenerator.Generate(ProjectDirectory);
                foreach (string Kind in new[] { "PARAMETERS", "PROFILES" })
                {
                    string Header = Path.Combine(Generated, Kind == "PARAMETERS" ? "Parameters.h" : "Profiles.inl").Replace('\\', '/');
                    PrivateDefinitions.Add("SCHOLA_RLTOOLS_PROJECT_" + Kind + "=\"" + Header + "\"");
                    ExternalDependencies.Add(Header);
                }
            }
            catch (Exception Error)
            {
                throw new BuildException("Schola RLtools configuration generation failed ({0}): {1}", Manifest, Error.Message);
            }
        }
    }
}
#endif

// Kept independent of Unreal APIs so the exact build generator is also tested as a small .NET executable.
internal static class ScholaRLToolsGenerator
{
    internal sealed class Profile
    {
        public string Name = "", Algorithm = "", ActorActivation = "ReLU", CriticActivation = "ReLU";
        public int Observations, Actions, ActorWidth = 32, CriticWidth = 32, ActorLayers = 3, CriticLayers = 3;
        public int Batch = 32, Replay = 4096, Rollout = 64;
        public SortedDictionary<string, string> Parameters = new(StringComparer.Ordinal);
        public string Id => Algorithm.ToLowerInvariant() + "/project/" + Name;
    }
    private sealed record Parameter(string Symbol, string Type, double Default, double Minimum, double Maximum, bool ExclusiveMinimum = false);
    private static readonly Dictionary<string, Parameter> Common = new()
    {
        ["discount"] = new("Discount", "float", .99, 0, 1, true)
    };
    private static readonly Dictionary<string, Parameter> OffPolicy = new()
    {
        ["targetTau"] = new("TargetTau", "float", .005, 0, 1, true),
        ["actorInterval"] = new("ActorInterval", "unsigned", 0, 0, Int32.MaxValue),
        ["targetInterval"] = new("TargetInterval", "unsigned", 0, 0, Int32.MaxValue)
    };
    private static readonly Dictionary<string, Parameter> SAC = new()
    {
        ["entropyCoefficient"] = new("EntropyCoefficient", "float", .5, 0, Single.MaxValue, true),
        ["adaptiveEntropy"] = new("AdaptiveEntropy", "bool", 1, 0, 1)
    };
    private static readonly Dictionary<string, Parameter> TD3 = new()
    {
        ["targetActionNoise"] = new("TargetActionNoise", "float", .2, 0, Single.MaxValue),
        ["targetActionNoiseClip"] = new("TargetActionNoiseClip", "float", .5, 0, Single.MaxValue)
    };
    private static readonly Dictionary<string, Parameter> PPO = new()
    {
        ["ppoClip"] = new("PPOClip", "float", .2, 0, Single.MaxValue, true),
        ["gae"] = new("GAE", "float", .95, 0, 1),
        ["ppoEntropyCoefficient"] = new("PPOEntropyCoefficient", "float", .01, 0, Single.MaxValue),
        ["initialActionStd"] = new("InitialActionStd", "float", .5, 0, Single.MaxValue, true),
        ["normalizeAdvantage"] = new("NormalizeAdvantage", "bool", 1, 0, 1),
        ["learnActionStd"] = new("LearnActionStd", "bool", 1, 0, 1)
    };

    private static void Check(bool Valid, string Where, string Message)
    {
        if (!Valid) throw new InvalidDataException(Where + ": " + Message);
    }
    private static void Object(JsonElement Value, string Where, IEnumerable<string> Allowed)
    {
        Check(Value.ValueKind == JsonValueKind.Object, Where, "expected an object");
        var Keys = new HashSet<string>(StringComparer.Ordinal);
        var Fields = new HashSet<string>(Allowed, StringComparer.Ordinal);
        foreach (var Field in Value.EnumerateObject())
        {
            Check(Keys.Add(Field.Name), Where, "duplicate field '" + Field.Name + "'");
            Check(Fields.Contains(Field.Name), Where, "unsupported field '" + Field.Name + "'");
        }
    }
    private static string Text(JsonElement Parent, string Field, string Where, string Fallback = "")
    {
        if (!Parent.TryGetProperty(Field, out var Value)) return Fallback;
        Check(Value.ValueKind == JsonValueKind.String, Where + "." + Field, "expected a string");
        return Value.GetString()!;
    }
    private static int Integer(JsonElement Parent, string Field, string Where, int Fallback, int Minimum, int Maximum = Int32.MaxValue)
    {
        if (!Parent.TryGetProperty(Field, out var Value)) return Fallback;
        Check(Value.ValueKind == JsonValueKind.Number && Value.TryGetInt32(out int _), Where + "." + Field, "expected a 32-bit integer");
        int Result = Value.GetInt32();
        Check(Result >= Minimum && Result <= Maximum, Where + "." + Field, $"must be in [{Minimum}, {Maximum}]");
        return Result;
    }
    private static string FloatLiteral(float Value)
    {
        string Text = Value.ToString("R", CultureInfo.InvariantCulture);
        if (!Text.Contains('.') && !Text.Contains('E') && !Text.Contains('e')) Text += ".0";
        return Text + "f";
    }
    private static string ReadParameter(JsonElement Parameters, string Key, Parameter Definition, string Where)
    {
        bool Present = Parameters.ValueKind == JsonValueKind.Object && Parameters.TryGetProperty(Key, out _);
        JsonElement Value = Present ? Parameters.GetProperty(Key) : default;
        if (Definition.Type == "bool")
        {
            Check(!Present || Value.ValueKind is JsonValueKind.True or JsonValueKind.False, Where, "expected a boolean");
            return (Present ? Value.GetBoolean() : Definition.Default != 0) ? "true" : "false";
        }
        if (Definition.Type == "unsigned")
            return Integer(Parameters, Key, Where[..Where.LastIndexOf('.')], (int)Definition.Default, 0).ToString(CultureInfo.InvariantCulture);
        double Number = Present && Value.ValueKind == JsonValueKind.Number && Value.TryGetDouble(out double Parsed) ? Parsed : Definition.Default;
        Check(!Present || Value.ValueKind == JsonValueKind.Number && Value.TryGetDouble(out _), Where, "expected a number");
        float Rounded = (float)Number;
        Check(Single.IsFinite(Rounded) && Rounded >= Definition.Minimum && Rounded <= Definition.Maximum
            && (!Definition.ExclusiveMinimum || Rounded > Definition.Minimum), Where, "outside the supported finite range");
        return FloatLiteral(Rounded);
    }

    internal static List<Profile> Parse(string Json)
    {
        // Select the string overload explicitly; UBT's rules references omit System.Memory.
        using var Document = JsonDocument.Parse(json: Json, options: new JsonDocumentOptions { MaxDepth = 16 });
        using var EmptyParameters = JsonDocument.Parse(json: "{}");
        var Root = Document.RootElement;
        Object(Root, "root", new[] { "schemaVersion", "profiles" });
        Check(Integer(Root, "schemaVersion", "root", -1, 1, 1) == 1, "schemaVersion", "expected schema version 1");
        Check(Root.TryGetProperty("profiles", out var Entries) && Entries.ValueKind == JsonValueKind.Array, "profiles", "expected an array");
        Check(Entries.GetArrayLength() <= 64, "profiles", "at most 64 project configurations per build");
        var Result = new List<Profile>();
        var Ids = new HashSet<string>(StringComparer.Ordinal);
        foreach (var Entry in Entries.EnumerateArray())
        {
            string Where = "profiles[" + Result.Count.ToString(CultureInfo.InvariantCulture) + "]";
            Object(Entry, Where, new[] { "name", "source", "algorithm", "observations", "actions", "network", "batchSize", "replayCapacity", "rolloutLength", "parameters" });
            var P = new Profile { Name = Text(Entry, "name", Where), Algorithm = Text(Entry, "algorithm", Where) };
            Check(P.Name.Length is >= 1 and <= 48 && P.Name[0] is >= 'a' and <= 'z'
                && P.Name.All(C => C is >= 'a' and <= 'z' or >= '0' and <= '9' or '-'), Where + ".name", "use 1..48 lowercase ASCII letters/digits/hyphens, starting with a letter");
            Check(P.Algorithm is "SAC" or "TD3" or "PPO", Where + ".algorithm", "expected SAC, TD3 or PPO");
            Check(Ids.Add(P.Id), Where, "duplicate configuration '" + P.Id + "'");
            Check(Text(Entry, "source", Where).Length <= 2048, Where + ".source", "source is too long");
            P.Observations = Integer(Entry, "observations", Where, 0, 1);
            P.Actions = Integer(Entry, "actions", Where, 0, 1);
            Check(P.Observations > 0 && P.Actions > 0, Where, "observations and actions are required positive dimensions");
            if (Entry.TryGetProperty("network", out var Network))
            {
                Object(Network, Where + ".network", new[] { "actorHiddenWidth", "criticHiddenWidth", "actorLayers", "criticLayers", "actorActivation", "criticActivation" });
                P.ActorWidth = Integer(Network, "actorHiddenWidth", Where + ".network", 32, 1);
                P.CriticWidth = Integer(Network, "criticHiddenWidth", Where + ".network", 32, 1);
                // The pinned MLP uses a C++ array of NUM_LAYERS-2; two total layers is not portable C++.
                P.ActorLayers = Integer(Network, "actorLayers", Where + ".network", 3, 3, 16);
                P.CriticLayers = Integer(Network, "criticLayers", Where + ".network", 3, 3, 16);
                P.ActorActivation = Text(Network, "actorActivation", Where + ".network", "ReLU");
                P.CriticActivation = Text(Network, "criticActivation", Where + ".network", "ReLU");
                Check(P.ActorActivation is "ReLU" or "Tanh", Where + ".network.actorActivation", "expected ReLU or Tanh");
                Check(P.CriticActivation is "ReLU" or "Tanh", Where + ".network.criticActivation", "expected ReLU or Tanh");
            }
            P.Batch = Integer(Entry, "batchSize", Where, 32, 2);
            if (P.Algorithm == "PPO")
            {
                Check(!Entry.TryGetProperty("replayCapacity", out _), Where + ".replayCapacity", "only applies to SAC/TD3");
                P.Rollout = Integer(Entry, "rolloutLength", Where, 64, P.Batch);
                Check(P.Rollout % P.Batch == 0, Where + ".rolloutLength", "must be divisible by batchSize");
            }
            else
            {
                Check(!Entry.TryGetProperty("rolloutLength", out _), Where + ".rolloutLength", "only applies to PPO");
                P.Replay = Integer(Entry, "replayCapacity", Where, 4096, P.Batch);
                Check(P.Replay >= P.Batch, Where + ".replayCapacity", "must be at least batchSize");
            }
            var Definitions = new Dictionary<string, Parameter>(Common);
            if (P.Algorithm != "PPO") foreach (var Item in OffPolicy) Definitions.Add(Item.Key, Item.Value);
            foreach (var Item in P.Algorithm == "SAC" ? SAC : P.Algorithm == "TD3" ? TD3 : PPO) Definitions.Add(Item.Key, Item.Value);
            JsonElement Parameters = default;
            if (Entry.TryGetProperty("parameters", out Parameters)) Object(Parameters, Where + ".parameters", Definitions.Keys);
            // Missing parameters are represented by an empty object for integer defaults.
            if (Parameters.ValueKind == JsonValueKind.Undefined) Parameters = EmptyParameters.RootElement;
            foreach (var Item in Definitions)
                P.Parameters.Add(Item.Value.Symbol, "static constexpr " + Item.Value.Type + " " + Item.Value.Symbol + " = "
                    + ReadParameter(Parameters, Item.Key, Item.Value, Where + ".parameters." + Item.Key) + ";");
            // Keep dimensions/products inside the 32-bit tensor/index range used by the adapter.
            long Output = P.Algorithm == "SAC" ? 2L * P.Actions : P.Actions;
            long Storage = P.Algorithm == "PPO" ? P.Rollout : P.Replay;
            long Columns = 2L * P.Observations + P.Actions + 4;
            Check(Columns <= Int32.MaxValue && Storage <= Int32.MaxValue / Columns, Where, "experience storage dimensions overflow the supported index range");
            foreach (var Pair in new[] { ((long)P.Observations, (long)P.ActorWidth), ((long)P.ActorWidth, Output),
                ((long)P.Observations + P.Actions, (long)P.CriticWidth), ((long)P.ActorWidth, (long)P.ActorWidth), ((long)P.CriticWidth, (long)P.CriticWidth),
                ((long)P.Batch, Math.Max(Math.Max((long)P.ActorWidth, P.CriticWidth), Math.Max((long)P.Observations + P.Actions, Output))) })
                Check(Pair.Item1 * Pair.Item2 <= Int32.MaxValue, Where, "network dimensions overflow the supported index range");
            Result.Add(P);
        }
        return Result.OrderBy(P => P.Id, StringComparer.Ordinal).ToList();
    }

    internal static string Generate(string ProjectDirectory)
    {
        string Manifest = Path.Combine(ProjectDirectory, "Config/ScholaRLTools.json");
        if (File.Exists(Manifest) && new FileInfo(Manifest).Length > 4 * 1024 * 1024)
            throw new InvalidDataException("Manifest exceeds 4 MiB");
        var Profiles = File.Exists(Manifest) ? Parse(File.ReadAllText(Manifest)) : new List<Profile>();
        var Parameters = new StringBuilder("// Generated by ScholaRLTools.Build.cs. Do not edit.\n#pragma once\nnamespace Schola::RLTools\n{\n");
        var Registration = new StringBuilder("// Generated by ScholaRLTools.Build.cs. Do not edit.\n");
        int Index = 0;
        foreach (var P in Profiles)
        {
            string Type = "FProjectConfiguration" + Index++.ToString(CultureInfo.InvariantCulture);
            Parameters.Append("struct ").Append(Type).Append(" : FAlgorithmParameters\n{\n");
            Parameters.Append("    static constexpr unsigned CriticHiddenWidth = ").Append(P.CriticWidth).Append(";\n");
            Parameters.Append("    static constexpr unsigned ActorLayers = ").Append(P.ActorLayers).Append(", CriticLayers = ").Append(P.CriticLayers).Append(";\n");
            Parameters.Append("    static constexpr EActivation ActorActivation = EActivation::").Append(P.ActorActivation)
                .Append(", CriticActivation = EActivation::").Append(P.CriticActivation).Append(";\n");
            foreach (string Declaration in P.Parameters.Values) Parameters.Append("    ").Append(Declaration).Append('\n');
            Parameters.Append("};\n");
            Registration.Append("SCHOLA_RLTOOLS_ALGORITHM_PROFILE(").Append(P.Algorithm).Append(", \"project/").Append(P.Name).Append("\", ")
                .Append(P.Observations).Append(", ").Append(P.Actions).Append(", ").Append(P.ActorWidth).Append(", ").Append(P.Batch).Append(", ")
                .Append(P.Replay).Append(", ").Append(P.Rollout).Append(", ").Append(Type).Append(")\n");
        }
        Parameters.Append("}\n");
        string ParameterText = Parameters.ToString(), RegistrationText = Registration.ToString();
        string Digest = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(ParameterText + RegistrationText))).ToLowerInvariant();
        string Output = Path.Combine(ProjectDirectory, "Intermediate/ScholaRLTools", Digest);
        Directory.CreateDirectory(Output);
        WriteIfChanged(Path.Combine(Output, "Parameters.h"), ParameterText);
        WriteIfChanged(Path.Combine(Output, "Profiles.inl"), RegistrationText);
        return Output;
    }
    private static void WriteIfChanged(string PathName, string Content)
    {
        if (File.Exists(PathName) && File.ReadAllText(PathName) == Content) return;
        string Temporary = PathName + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(Temporary, Content, new UTF8Encoding(false));
            File.Move(Temporary, PathName, true);
        }
        finally { if (File.Exists(Temporary)) File.Delete(Temporary); }
    }
}
