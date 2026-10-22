#!/usr/bin/env bash
# Build the editor target (needed for -game runs and the editor).
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
exec "$ENGINE/Engine/Build/BatchFiles/Linux/Build.sh" BackgroundNPCsEditor Linux Development \
    -Project="$PROJECT" -NoHotReloadFromIDE -NoUBA "$@"
