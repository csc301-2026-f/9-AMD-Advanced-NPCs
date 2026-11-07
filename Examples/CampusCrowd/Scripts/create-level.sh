#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
exec "$ENGINE/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PROJECT" \
    -run=pythonscript -script="$PROJECT_DIR/Tools/create_level.py" \
    -unattended -nullrhi -nosound -nop4 -NoSplash "$@"
