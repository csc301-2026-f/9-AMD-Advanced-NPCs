#!/usr/bin/env bash
# Record each mode for 30 s (X11 + ffmpeg) and combine the clips side by side.
# Usage: Scripts/record.sh [output-dir] [extra game arguments...]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
OUT="$(realpath -m "${1:-$PROJECT_DIR/Saved/Recording}")"; shift || true
[[ -x "$GAME" ]] || { echo "Run Scripts/package.sh first." >&2; exit 1; }
command -v ffmpeg >/dev/null && command -v xdotool >/dev/null || { echo "Needs ffmpeg and xdotool on X11." >&2; exit 1; }
mkdir -p "$OUT"
for MODE in Sync Background; do
    echo "Recording $MODE..."
    "$GAME" -windowed -WinX=0 -WinY=0 -ResX=1280 -ResY=720 -nosound -Mode="$MODE" "$@" > /dev/null &
    # The launcher script runs the game as a child process; find its window by title.
    WINDOW="$(xdotool search --sync --onlyvisible --name '^BackgroundNPCs ' | head -1)"
    PID="$(xdotool getwindowpid "$WINDOW")"
    sleep 12  # let the crowd spread out and the HUD's one-second averages settle
    ffmpeg -loglevel error -y -f x11grab -framerate 60 -window_id "$WINDOW" -i "$DISPLAY" -t 30 \
        -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p "$OUT/${MODE,,}.mp4"
    kill "$PID"; wait 2>/dev/null || true
done
ffmpeg -loglevel error -y -i "$OUT/sync.mp4" -i "$OUT/background.mp4" \
    -filter_complex "[0:v]scale=1280:720,setsar=1[a];[1:v]scale=1280:720,setsar=1[b];[a][b]hstack" \
    -c:v libx264 -preset slow -crf 30 -pix_fmt yuv420p -movflags +faststart "$OUT/side-by-side.mp4"
echo "$OUT/side-by-side.mp4"
