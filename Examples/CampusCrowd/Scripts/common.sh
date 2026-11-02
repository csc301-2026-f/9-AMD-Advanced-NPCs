#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$PROJECT_DIR/CampusCrowd.uproject"
ENGINE="${UE_ENGINE_ROOT:-}"
if [[ ! -x "$ENGINE/Engine/Build/BatchFiles/Linux/Build.sh" ]]; then
    echo "Set UE_ENGINE_ROOT to an Unreal Engine 5.8 installation." >&2
    exit 1
fi
# Link individual plugin directories: linking the repository itself creates a
# recursive Plugins/Schola/Examples/... loop in Unreal Editor's file watcher.
PLUGIN_DIR="$PROJECT_DIR/Plugins/Schola"
mkdir -p "$PROJECT_DIR/Plugins"
if [[ -L "$PLUGIN_DIR" ]]; then rm "$PLUGIN_DIR"; fi
mkdir -p "$PLUGIN_DIR"
for PART in Schola.uplugin Source Config Resources Binaries Intermediate; do
    if [[ ! -e "$PLUGIN_DIR/$PART" && ! -L "$PLUGIN_DIR/$PART" ]]; then
        ln -s "../../../../$PART" "$PLUGIN_DIR/$PART"
    fi
done
PACKAGE_DIR="$PROJECT_DIR/Saved/Package"
GAME="$PACKAGE_DIR/Linux/CampusCrowd.sh"
