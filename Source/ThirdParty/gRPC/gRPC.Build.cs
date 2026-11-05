// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

public class gRPC : ModuleRules
{
    private static readonly string[] RequiredWin64Libraries =
    {
        "address_sorting.lib",
        "cares.lib",
        "gpr.lib",
        "grpc.lib",
        "grpc++.lib",
        "libprotobuf.lib",
        "libutf8_validity.lib",
        "re2.lib",
        "upb_base_lib.lib",
        "upb_hash_lib.lib",
        "upb_json_lib.lib",
        "upb_lex_lib.lib",
        "upb_mem_lib.lib",
        "upb_message_lib.lib",
        "upb_mini_descriptor_lib.lib",
        "upb_mini_table_lib.lib",
        "upb_reflection_lib.lib",
        "upb_textformat_lib.lib",
        "upb_wire_lib.lib",
        "utf8_range_lib.lib",
    };

    public gRPC(ReadOnlyTargetRules Target)
        : base(Target)
    {
        Type = ModuleType.External;

        string platformPath = Path.Combine(ModuleDirectory, Target.Platform.ToString());
        string includePath = Path.Combine(platformPath, "include");
        string libraryPath = Path.Combine(platformPath, "lib");
        string dependencyBuildScript = Target.Platform == UnrealTargetPlatform.Mac
            ? "Resources/Build/mac_dependencies.sh"
            : Target.Platform == UnrealTargetPlatform.Linux
                ? "Resources/Build/linux_dependencies.sh"
                : "Resources/Build/windows_dependencies.bat";

        EnsureDirectoryExists(
            includePath,
            $"Missing gRPC headers for {Target.Platform} at {includePath}. Rebuild third-party dependencies with {dependencyBuildScript}."
        );

        PublicSystemIncludePaths.Add(includePath);

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            AddWindowsLibraries(libraryPath);
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            AddLibrariesFromDirectory(libraryPath, "*.a", "Linux gRPC archives", dependencyBuildScript);
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            AddLibrariesFromDirectory(libraryPath, "*.a", "Mac gRPC archives", dependencyBuildScript);
        }
        else
        {
            throw new Exception("Unsupported platform " + Target.Platform.ToString());
        }

        PublicDependencyModuleNames.AddRange(new string[] { "OpenSSL", "zlib" });
        PrivateDependencyModuleNames.AddRange(new string[] { });

        PublicDefinitions.Add("GOOGLE_PROTOBUF_NO_RTTI=1");
    }

    private void AddWindowsLibraries(string libraryPath)
    {
        string[] presentLibraries = GetLibrariesFromDirectory(
            libraryPath,
            "*.lib",
            "Windows gRPC/protobuf libraries",
            "Resources/Build/windows_dependencies.bat");
        HashSet<string> presentNames = new HashSet<string>(
            presentLibraries.Select(Path.GetFileName),
            StringComparer.OrdinalIgnoreCase
        );

        string[] missingLibraries = RequiredWin64Libraries
            .Where(fileName => !presentNames.Contains(fileName))
            .ToArray();

        if (missingLibraries.Length > 0)
        {
            throw new BuildException(
                $"Missing required Windows gRPC/protobuf archives in {libraryPath}: {string.Join(", ", missingLibraries)}. Rebuild third-party dependencies with Resources/Build/windows_dependencies.bat."
            );
        }

        string[] abslLibraries = presentLibraries
            .Select(Path.GetFileName)
            .Where(fileName => fileName.StartsWith("absl_", StringComparison.OrdinalIgnoreCase))
            .OrderBy(fileName => fileName, StringComparer.OrdinalIgnoreCase)
            .ToArray();

        if (abslLibraries.Length == 0)
        {
            throw new BuildException(
                $"No Abseil static libraries were found in {libraryPath}. Rebuild third-party dependencies with Resources/Build/windows_dependencies.bat."
            );
        }

        foreach (string fileName in RequiredWin64Libraries.Concat(abslLibraries))
        {
            PublicAdditionalLibraries.Add(Path.Combine(libraryPath, fileName));
        }
    }

    private void AddLibrariesFromDirectory(
        string libraryPath,
        string searchPattern,
        string description,
        string dependencyBuildScript)
    {
        foreach (string libraryPathEntry in GetLibrariesFromDirectory(
            libraryPath,
            searchPattern,
            description,
            dependencyBuildScript))
        {
            PublicAdditionalLibraries.Add(libraryPathEntry);
        }
    }

    private static string[] GetLibrariesFromDirectory(
        string libraryPath,
        string searchPattern,
        string description,
        string dependencyBuildScript)
    {
        EnsureDirectoryExists(
            libraryPath,
            $"Missing {description} directory at {libraryPath}. Rebuild third-party dependencies with {dependencyBuildScript}."
        );

        string[] libraries = Directory
            .GetFiles(libraryPath, searchPattern)
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
            .ToArray();

        if (libraries.Length == 0)
        {
            throw new BuildException(
                $"No {description} were found in {libraryPath}. Rebuild third-party dependencies with {dependencyBuildScript}."
            );
        }

        return libraries;
    }

    private static void EnsureDirectoryExists(string path, string errorMessage)
    {
        if (!Directory.Exists(path))
        {
            throw new BuildException(errorMessage);
        }
    }
}
