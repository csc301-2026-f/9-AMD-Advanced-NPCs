# macOS setup

Apple Silicon setup for building Schola with Unreal Engine 5.8. These steps install the Mac gRPC dependencies and prepare a plugin layout UnrealBuildTool can compile. They are not specific to one example.

This was verified on Apple Silicon with Unreal Engine 5.8 and Xcode 26.1.1. The `BackgroundNPCsEditor` target is the project used for that check. Its example-specific controls are in [Examples/BackgroundNPCs/README.md](../Examples/BackgroundNPCs/README.md).

## Requirements

- An Apple Silicon Mac.
- Unreal Engine 5.8. Set `UE_ENGINE_ROOT` to the engine directory that contains `Engine/Build`.
- Xcode, including the Metal toolchain. Xcode 26 does not always install that toolchain until the command below has been run.
- Git, CMake, the Xcode command-line tools, and either Ninja or Make.

Unreal Engine 5.8 links Mac editor binaries for macOS 14.0. `Resources/Build/mac_dependencies.sh` pins the gRPC build to that deployment target so the archives match the engine.

## Metal toolchain

```bash
xcodebuild -downloadComponent MetalToolchain
```

Run this before the first shader compile.

## gRPC and protobuf

Windows and Linux gRPC archives are already in the checkout. The Mac archives are not. From the repository root:

```bash
Resources/Build/mac_dependencies.sh
```

The script clones gRPC v1.80.0, checks that the bundled protobuf is 31.1, and builds static arm64 libraries with Apple Clang. It installs headers and archives to:

- `Source/ThirdParty/gRPC/Mac/include`
- `Source/ThirdParty/gRPC/Mac/lib`

That directory is gitignored. The script does not modify `Source/ThirdParty/gRPC/Linux` or `Source/ThirdParty/gRPC/Win64`. It is safe to run again. The clone and build directory defaults to the system temporary directory. Override it with `SCHOLA_GRPC_WORK_ROOT` if needed.

## Plugin preparation

UnrealBuildTool discovers a plugin only when `Schola.uplugin` is inside a child of a plugin directory. The descriptor at the repository root is not found by pointing `AdditionalPluginDirectories` at this repository.

For an existing game, copy this repository to that project's `Plugins/Schola` directory, then recompile. See [README-Schola.md](../README-Schola.md).

For an example that already lives inside this checkout, do not symlink `Plugins/Schola` to the repository root. That symlink walks back through `Examples/.../Plugins/Schola` and recurses. `Examples/BackgroundNPCs/Scripts/prepare.sh` creates a gitignored `Plugins/Schola` directory instead: it copies `Schola.uplugin` and relatively links `Config`, `Content`, `Resources`, `Shaders`, and each `Source/<module>`.

```bash
cd Examples/BackgroundNPCs
Scripts/prepare.sh
```

Run it again after pulling source changes. All of its links are relative to the script.

## Build

Build the editor with Unreal Build Accelerator disabled:

```bash
export UE_ENGINE_ROOT=/path/to/UE_5.8
"$UE_ENGINE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" <EditorTarget> Mac Development \
    -Project="/absolute/path/to/Project.uproject" -NoHotReloadFromIDE -NoUBA
```

For the verified Background NPCs project, `<EditorTarget>` is `BackgroundNPCsEditor` and the project file is `Examples/BackgroundNPCs/BackgroundNPCs.uproject`.

Open the editor after the build:

```bash
open "$UE_ENGINE_ROOT/Engine/Binaries/Mac/UnrealEditor.app" --args "/absolute/path/to/Project.uproject"
```
