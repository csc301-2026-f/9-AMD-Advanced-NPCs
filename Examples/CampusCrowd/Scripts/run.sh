#!/usr/bin/env bash
# Play the comparison. Uses the packaged game if present, otherwise the editor in -game mode.
# Example: Scripts/run.sh -Mode=Sync -Agents=10000
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
ARGS=(-windowed -nosound)
HAS_RES_X=0
HAS_RES_Y=0
for ARG in "$@"; do
    case "$ARG" in -ResX=*|-resx=*) HAS_RES_X=1 ;; -ResY=*|-resy=*) HAS_RES_Y=1 ;; esac
done
if [[ "$HAS_RES_X" == 0 ]]; then ARGS+=(-ResX=1600); fi
if [[ "$HAS_RES_Y" == 0 ]]; then ARGS+=(-ResY=900); fi
ARGS+=("$@")
if [[ -x "$GAME" ]]; then
    exec "$GAME" "${ARGS[@]}"
fi
exec "$ENGINE/Engine/Binaries/Linux/UnrealEditor" "$PROJECT" -game "${ARGS[@]}"
