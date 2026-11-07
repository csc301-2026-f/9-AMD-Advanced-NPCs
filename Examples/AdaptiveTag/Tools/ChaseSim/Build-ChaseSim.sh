#!/bin/sh
# Builds chase_sim (console front end for ChaseSession) on macOS / Linux with clang++ or g++.
# Flags mirror Build-ChaseSim.cmd and OnlineSACRuntime.Build.cs: C++20, no exceptions, no RTTI, RL_TOOLS_DEBUG.
# Usage: [EXTRA_FLAGS="-DONLINE_SAC_LR=0.003"] Build-ChaseSim.sh [output dir]   (default: Saved/ChaseSim)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../Source/OnlineSACRuntime"
OUT=${1:-"$HERE/../../Saved/ChaseSim"}
CXX=${CXX:-$(command -v clang++ || command -v g++)}
[ -n "$CXX" ] || { echo "No C++ compiler found (install clang++ or g++)." >&2; exit 1; }
mkdir -p "$OUT"
"$CXX" -std=c++20 -O2 -fno-rtti -fno-exceptions -ffp-contract=off -DRL_TOOLS_DEBUG $EXTRA_FLAGS \
  -I "$SRC/Public" -I "$SRC/Private" -isystem "$HERE/../../../../Source/ThirdParty/RLTools/include" \
  "$HERE/Main.cpp" "$SRC/Private/OnlineSACRuntime.cpp" "$SRC/Private/ChaseConfig.cpp" "$SRC/Private/RunnerPatterns.cpp" \
  "$SRC/Private/ChaseWorld.cpp" "$SRC/Private/ChaseSession.cpp" \
  -o "$OUT/chase_sim"
echo "Built $OUT/chase_sim"
