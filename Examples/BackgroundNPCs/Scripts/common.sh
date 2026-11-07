#!/usr/bin/env bash
# Shared setup for the Linux scripts. Set UE_ENGINE_ROOT to your engine directory.
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/BackgroundNPCs.uproject"
ENGINE="${UE_ENGINE_ROOT:-}"
if [[ -z "$ENGINE" || ! -x "$ENGINE/Engine/Build/BatchFiles/Linux/Build.sh" ]]; then
    echo "Set UE_ENGINE_ROOT to an Unreal Engine 5.8 directory (containing Engine/Build)." >&2
    exit 1
fi
# The example uses the Schola plugin from this repository's root.
mkdir -p "$PROJECT_DIR/Plugins"
if [[ ! -e "$PROJECT_DIR/Plugins/Schola/Schola.uplugin" ]]; then
    ln -sfn ../../.. "$PROJECT_DIR/Plugins/Schola"
fi
PACKAGE_DIR="$PROJECT_DIR/Saved/Package"
GAME="$PACKAGE_DIR/Linux/BackgroundNPCs.sh"
