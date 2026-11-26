#!/bin/sh
# Builds ChaseAdaptation (the D3 chase adaptation test on the plugin's FLearner, no Unreal) and runs it
# on several seeds in parallel. Criterion 5 passes only if every seed passes criteria 1-4.
# Usage: [SEEDS="1 2 3"] [JOBS=3] Tools/ScholaRLToolsNative/run-chase.sh [outdir] [-- ChaseAdaptation args]
#   e.g. SEEDS="4 5 6" Tools/ScholaRLToolsNative/run-chase.sh Saved/chase-r20k -- --replay 20000
# CHECKPOINT=1 runs each seed twice: seed<N>-save saves a FullResume checkpoint after pretraining, then
# seed<N>-start loads it in a fresh process into an adaptive learner and a frozen control. Criterion 5
# then needs both runs of every seed to pass.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT="$REPO/Saved/ChaseAdaptation"
if [ $# -gt 0 ] && [ "$1" != "--" ]; then OUT=$1; shift; fi
[ $# -gt 0 ] && [ "$1" = "--" ] && shift
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
SEEDS=${SEEDS:-"1 2 3"}
JOBS=${JOBS:-3}
CHECKPOINT=${CHECKPOINT:-0}
CXX=${CXX:-}
if [ -z "$CXX" ]; then
    if command -v clang++ >/dev/null 2>&1; then CXX=clang++; else CXX=g++; fi
fi
LEARNING="$REPO/Source/ScholaRLTools/Private/Learning"
CHASE="$REPO/Examples/AdaptiveTag/Source/OnlineSACRuntime"
BIN="$OUT/ChaseAdaptation"
echo "Building with $CXX into $OUT"
"$CXX" -std=c++20 -O2 -fno-rtti -fno-exceptions -ffp-contract=off -DRL_TOOLS_DEBUG -pthread \
    "-DSCHOLA_RLTOOLS_PROJECT_PROFILES=\"$HERE/ChaseProfiles.inl\"" \
    -I "$LEARNING" -I "$CHASE/Public" -isystem "$REPO/Source/ThirdParty/RLTools/include" \
    "$HERE/ChaseAdaptation.cpp" "$LEARNING/RLToolsLearner.cpp" \
    "$CHASE/Private/ChaseWorld.cpp" "$CHASE/Private/RunnerPatterns.cpp" "$CHASE/Private/ChaseConfig.cpp" \
    -o "$BIN"

# Run seeds JOBS at a time; each writes seed<N>.txt and seed<N>.csv.
RUNNING=0
# Run NAME ARGS... runs the binary into NAME.txt and records its exit code in NAME.exit.
# A failed criterion exits 1; keep going so the start run still happens (set -e would stop here).
Run() {
    NAME=$1; shift
    if "$BIN" "$@" > "$OUT/$NAME.txt" 2>&1; then CODE=0; else CODE=$?; fi
    echo "$CODE" > "$OUT/$NAME.exit"
}
RunSeed() {
    S=$1; shift
    if [ "$CHECKPOINT" = 1 ]; then
        # Remove any old file first so a save that never got that far cannot leave a stale checkpoint.
        rm -f "$OUT/seed$S.ckpt"
        Run "seed$S-save" --seed "$S" --csv "$OUT/seed$S-save.csv" --save-checkpoint "$OUT/seed$S.ckpt" "$@"
        if [ -f "$OUT/seed$S.ckpt" ]; then
            Run "seed$S-start" --seed "$S" --csv "$OUT/seed$S-start.csv" --start-checkpoint "$OUT/seed$S.ckpt" "$@"
        else
            echo "error: seed $S saved no checkpoint" > "$OUT/seed$S-start.txt"
            echo 1 > "$OUT/seed$S-start.exit"
        fi
    else
        Run "seed$S" --seed "$S" --csv "$OUT/seed$S.csv" "$@"
    fi
}
for SEED in $SEEDS; do
    RunSeed "$SEED" "$@" &
    RUNNING=$((RUNNING + 1))
    if [ "$RUNNING" -ge "$JOBS" ]; then wait; RUNNING=0; fi
done
wait

FAILED=0
for SEED in $SEEDS; do
    if [ "$CHECKPOINT" = 1 ]; then RUNS="seed$SEED-save seed$SEED-start"; else RUNS="seed$SEED"; fi
    for RUN in $RUNS; do
        sed -n '/^RESULT/,$p' "$OUT/$RUN.txt"
        grep -q '^FAIL\|^error' "$OUT/$RUN.txt" && FAILED=1
        grep -q '^RESULT' "$OUT/$RUN.txt" || FAILED=1
        CODE=$(cat "$OUT/$RUN.exit" 2>/dev/null || echo missing)
        [ "$CODE" = 0 ] || { echo "FAIL $RUN exited with $CODE"; FAILED=1; }
    done
done
if [ "$FAILED" -eq 0 ]; then echo "PASS criterion5: criteria 1-4 hold on seeds $SEEDS"; else echo "FAIL criterion5: see above"; fi
exit "$FAILED"
