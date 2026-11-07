#!/bin/sh
# Builds and runs the engine-free Schola RLtools checkpoint tests (no Unreal needed; macOS/Linux).
# Usage: Tools/ScholaRLToolsNative/build.sh [outdir]   (default: <repo>/Saved/ScholaRLToolsNative)
# Prints PASS/FAIL per check; exits non-zero if the build or any check fails.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=${1:-"$REPO/Saved/ScholaRLToolsNative"}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
CXX=${CXX:-}
if [ -z "$CXX" ]; then
    if command -v clang++ >/dev/null 2>&1; then CXX=clang++; else CXX=g++; fi
fi
LEARNING="$REPO/Source/ScholaRLTools/Private/Learning"
echo "Building with $CXX into $OUT"
"$CXX" -std=c++20 -O1 -fno-rtti -fno-exceptions -ffp-contract=off -DRL_TOOLS_DEBUG -pthread \
    -I "$LEARNING" -isystem "$REPO/Source/ThirdParty/RLTools/include" \
    "$HERE/CheckpointTests.cpp" "$LEARNING/RLToolsLearner.cpp" \
    -o "$OUT/CheckpointTests"
rm -rf "$OUT/checkpoints"
"$OUT/CheckpointTests" "$OUT/checkpoints"
