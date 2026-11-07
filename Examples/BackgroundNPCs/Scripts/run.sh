#!/usr/bin/env bash
# Play the comparison. Uses the packaged game if present, otherwise the editor in -game mode.
# Example: Scripts/run.sh -Mode=Sync -Agents=10000
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
ARGS=(-windowed -ResX=1600 -ResY=900 -nosound "$@")
if [[ -x "$GAME" ]]; then
    exec "$GAME" "${ARGS[@]}"
fi
exec "$ENGINE/Engine/Binaries/Linux/UnrealEditor" "$PROJECT" -game "${ARGS[@]}"
