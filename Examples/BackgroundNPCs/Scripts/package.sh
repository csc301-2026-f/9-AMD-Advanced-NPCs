#!/usr/bin/env bash
# Build, cook, and stage a standalone Development game in Saved/Package.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
exec "$ENGINE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$PROJECT" -noP4 \
    -platform=Linux -clientconfig=Development -build -cook -stage -pak -archive \
    -archivedirectory="$PACKAGE_DIR" -map=/Engine/Maps/Entry -unattended -utf8output -ubtargs="-NoUBA" "$@"
