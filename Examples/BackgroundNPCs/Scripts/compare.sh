#!/usr/bin/env bash
# Measure both modes one after another with identical settings, then summarize.
# Usage: Scripts/compare.sh [output-dir] [extra game arguments...]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
OUT="$(realpath -m "${1:-$PROJECT_DIR/Saved/Comparison}")"; shift || true
[[ -x "$GAME" ]] || { echo "Run Scripts/package.sh first." >&2; exit 1; }
mkdir -p "$OUT"
for MODE in Sync Background; do
    echo "Measuring $MODE..."
    "$GAME" -windowed -ResX=1280 -ResY=720 -nosound -Mode="$MODE" -WarmupSeconds=10 -CaptureSeconds=30 \
        -Report="$OUT/${MODE,,}.json" -abslog="$OUT/${MODE,,}.log" "$@" > /dev/null
done
python3 "$PROJECT_DIR/Tools/summarize.py" "$OUT/sync.json" "$OUT/background.json" | tee "$OUT/summary.md"
