#!/usr/bin/env bash
# Open the saved campus map in Unreal Editor.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
exec "$ENGINE/Engine/Binaries/Linux/UnrealEditor" "$PROJECT" "$@"
