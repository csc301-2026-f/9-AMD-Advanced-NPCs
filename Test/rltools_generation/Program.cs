// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Globalization;

static class Program
{
    static int Checks;
    static void Require(bool Condition, string Message)
    {
        if (!Condition) throw new Exception(Message);
        ++Checks;
    }
    static JsonObject Configuration(string Name = "guards", string Algorithm = "SAC") => new()
    {
        ["schemaVersion"] = 1,
        ["profiles"] = new JsonArray(new JsonObject { ["name"] = Name, ["algorithm"] = Algorithm, ["observations"] = 6, ["actions"] = 3 })
    };
    static void Reject(string Json, string Diagnostic)
    {
        try { ScholaRLToolsGenerator.Parse(Json); throw new Exception("Invalid request accepted: " + Json); }
        catch (Exception Error) when (Error is InvalidDataException or JsonException)
        { Require(Error.Message.Contains(Diagnostic, StringComparison.Ordinal), "Wrong diagnostic: " + Error.Message); }
    }
    static void MutateReject(Action<JsonObject> Mutate, string Diagnostic)
    {
        var Root = Configuration();
        Mutate(Root["profiles"]![0]!.AsObject());
        Reject(Root.ToJsonString(), Diagnostic);
    }
    static void Write(string Project, JsonObject Json)
    {
        Directory.CreateDirectory(Path.Combine(Project, "Config"));
        File.WriteAllText(Path.Combine(Project, "Config/ScholaRLTools.json"), Json.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
    }
    static int Main(string[] Args)
    {
        if (Args.Length == 2 && Args[0] == "--generate")
        { Console.WriteLine(ScholaRLToolsGenerator.Generate(Path.GetFullPath(Args[1]))); return 0; }
        if (Args.Length == 2 && Args[0] == "--check-manifest")
        {
            var Profiles = ScholaRLToolsGenerator.Parse(File.ReadAllText(Args[1]));
            Console.WriteLine("Validated editor request: " + String.Join(", ", Profiles.Select(P => P.Id)));
            return 0;
        }
        string Root = Directory.CreateTempSubdirectory("ScholaRLToolsGeneration-").FullName;
        try
        {
            foreach (string Algorithm in new[] { "SAC", "TD3", "PPO" })
            {
                var Parsed = ScholaRLToolsGenerator.Parse(Configuration(Algorithm: Algorithm).ToJsonString());
                Require(Parsed.Count == 1 && Parsed[0].Algorithm == Algorithm && Parsed[0].Observations == 6 && Parsed[0].Actions == 3, "Algorithm/shape parsing");
            }
            Reject("{\"schemaVersion\":1,\"schemaVersion\":1,\"profiles\":[]}", "duplicate");
            Reject("{\"schemaVersion\":2,\"profiles\":[]}", "schemaVersion");
            Reject("{\"profiles\":[]}", "schemaVersion");
            Reject("{\"schemaVersion\":1,\"profiles\":{}}", "profiles");
            var Duplicate = Configuration();
            Duplicate["profiles"]!.AsArray().Add(Duplicate["profiles"]![0]!.DeepClone());
            Reject(Duplicate.ToJsonString(), "duplicate configuration");
            MutateReject(P => P["observations"] = 0, "observations");
            MutateReject(P => P["actions"] = -2, "actions");
            MutateReject(P => P["observations"] = 2.5, "integer");
            MutateReject(P => P["actions"] = "three", "integer");
            MutateReject(P => P["algorithm"] = "DQN", "algorithm");
            MutateReject(P => P["name"] = "x\"); #error injected", "name");
            MutateReject(P => P["recurrent"] = true, "unsupported");
            MutateReject(P => P["batchSize"] = 1, "batchSize");
            MutateReject(P => { P["batchSize"] = 128; P["replayCapacity"] = 64; }, "replayCapacity");
            MutateReject(P => P["rolloutLength"] = 64, "only applies");
            MutateReject(P => P["network"] = new JsonObject { ["actorLayers"] = 1 }, "actorLayers");
            MutateReject(P => P["network"] = new JsonObject { ["criticLayers"] = 2 }, "criticLayers");
            MutateReject(P => P["network"] = new JsonObject { ["criticLayers"] = 17 }, "criticLayers");
            MutateReject(P => P["network"] = new JsonObject { ["actorActivation"] = "GRU" }, "actorActivation");
            MutateReject(P => P["network"] = new JsonObject { ["actorHiddenWidth"] = Int32.MaxValue }, "overflow");
            MutateReject(P => { P["observations"] = Int32.MaxValue; P["replayCapacity"] = Int32.MaxValue; }, "overflow");
            MutateReject(P => { P["observations"] = 1; P["actions"] = 1000000000; P["batchSize"] = 2; P["replayCapacity"] = 2;
                P["network"] = new JsonObject { ["actorHiddenWidth"] = 1, ["criticHiddenWidth"] = 1 }; }, "overflow");
            foreach (double Invalid in new[] { 0.0, -0.1, 1.1, 1e100 })
                MutateReject(P => P["parameters"] = new JsonObject { ["discount"] = Invalid }, "discount");
            MutateReject(P => P["parameters"] = new JsonObject { ["adaptiveEntropy"] = 1 }, "boolean");
            MutateReject(P => P["parameters"] = new JsonObject { ["actorInterval"] = -1 }, "actorInterval");
            MutateReject(P => P["parameters"] = new JsonObject { ["ppoClip"] = .2 }, "unsupported");
            var PPO = Configuration(Algorithm: "PPO");
            PPO["profiles"]![0]!["rolloutLength"] = 63;
            Reject(PPO.ToJsonString(), "divisible");
            PPO = Configuration(Algorithm: "PPO");
            PPO["profiles"]![0]!["replayCapacity"] = 512;
            Reject(PPO.ToJsonString(), "only applies");

            string A = Path.Combine(Root, "ProjectA"), B = Path.Combine(Root, "ProjectB");
            Write(A, Configuration());
            string Generated = ScholaRLToolsGenerator.Generate(A);
            string Header = Path.Combine(Generated, "Parameters.h");
            string Original = File.ReadAllText(Header);
            var OldTime = new DateTime(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc);
            File.SetLastWriteTimeUtc(Header, OldTime);
            Require(ScholaRLToolsGenerator.Generate(A) == Generated && File.GetLastWriteTimeUtc(Header) == OldTime, "Idempotent generation rewrote headers");
            var Formatted = Configuration();
            Formatted["profiles"]![0]!["source"] = "untrusted source\n#error injected";
            Write(A, Formatted);
            Require(ScholaRLToolsGenerator.Generate(A) == Generated, "Noncompiled metadata changed generated code");
            Require(!File.ReadAllText(Header).Contains("injected", StringComparison.Ordinal), "Source text entered C++");
            var Changed = Configuration();
            Changed["profiles"]![0]!["observations"] = 9;
            Changed["profiles"]![0]!["network"] = new JsonObject { ["actorHiddenWidth"] = 24, ["criticHiddenWidth"] = 16, ["actorLayers"] = 4, ["actorActivation"] = "Tanh" };
            Changed["profiles"]![0]!["parameters"] = new JsonObject { ["discount"] = .9 };
            Write(A, Changed);
            string Updated = ScholaRLToolsGenerator.Generate(A);
            Require(Updated != Generated && File.ReadAllText(Path.Combine(Updated, "Parameters.h")).Contains("Discount = 0.9f", StringComparison.Ordinal), "Changed settings did not change compiled output");
            Write(B, Configuration(Name: "other", Algorithm: "TD3"));
            string Other = ScholaRLToolsGenerator.Generate(B);
            Require(Other.StartsWith(B, StringComparison.Ordinal) && Updated.StartsWith(A, StringComparison.Ordinal), "Output escaped the owning project");
            Require(!File.ReadAllText(Path.Combine(Other, "Profiles.inl")).Contains("guards", StringComparison.Ordinal), "Projects leaked configurations");
            Write(A, Configuration());
            var InvalidRoot = Configuration();
            InvalidRoot["profiles"]![0]!["actions"] = 0;
            Write(A, InvalidRoot);
            try { ScholaRLToolsGenerator.Generate(A); throw new Exception("Invalid generation succeeded"); }
            catch (InvalidDataException) { Require(File.ReadAllText(Header) == Original, "Invalid generation replaced previous output"); }
            var Empty = new JsonObject { ["schemaVersion"] = 1, ["profiles"] = new JsonArray() };
            Write(A, Empty);
            string Removed = ScholaRLToolsGenerator.Generate(A);
            Require(!File.ReadAllText(Path.Combine(Removed, "Profiles.inl")).Contains("SCHOLA_RLTOOLS_ALGORITHM_PROFILE", StringComparison.Ordinal), "Removed profiles remain registered");
            File.Delete(Path.Combine(A, "Config/ScholaRLTools.json"));
            Require(ScholaRLToolsGenerator.Generate(A) == Removed, "Absent manifest differs from an empty manifest");
            Console.WriteLine($"PASS: {Checks} generator checks");
            return 0;
        }
        finally
        {
            // This directory was created above, directly under the OS temp directory.
            if (!Path.GetFullPath(Root).StartsWith(Path.GetFullPath(Path.GetTempPath()), StringComparison.OrdinalIgnoreCase))
                throw new Exception("Refusing cleanup outside temporary directory");
            Directory.Delete(Root, true);
        }
    }
}
