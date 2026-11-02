#!/usr/bin/env bash
# Render a 20-second, 1080p Schola background-inference flight from the game window on X11.
# Usage: Scripts/render-video.sh [output.mp4]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
for TOOL in python3 ffmpeg ffprobe xdotool fc-match; do
    command -v "$TOOL" >/dev/null || { echo "Missing $TOOL" >&2; exit 1; }
done
OUTPUT="$(realpath -m "${1:-$PROJECT_DIR/Evidence/campus-crowd-background-20s.mp4}")"
exec python3 "$PROJECT_DIR/Tools/render_demo_video.py" \
    --project-dir "$PROJECT_DIR" --output "$OUTPUT"
