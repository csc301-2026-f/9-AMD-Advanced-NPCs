#!/usr/bin/env bash
# Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#
# Single entry point for the North Campus crowd demo.
#
#   ./demo.sh                     build (incremental) and play
#   ./demo.sh help                list every command
#
# Requires UE_ENGINE_ROOT to point at an Unreal Engine 5.8 installation.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"
NAME="CampusCrowd"
PROJECT="$PROJECT_DIR/$NAME.uproject"
PACKAGE_DIR="$PROJECT_DIR/Saved/Package"
PACKAGED_GAME="$PACKAGE_DIR/Linux/$NAME.sh"
MAP="/Game/Maps/NorthCampus"

usage() {
	cat <<USAGE
North Campus crowd demo: up to 100,000 Mass students steered by one ONNX policy
through Schola's background NPC API.

Usage: ./demo.sh [command] [options] [game arguments...]

Commands:
  run [--packaged] [--no-build]   Build the editor target and play (default command).
                                  --packaged plays the package from './demo.sh package' instead.
  build                           Build the editor target.
  editor                          Build, then open the project in Unreal Editor.
  package                         Build, cook and stage a standalone game in Saved/Package.
  measure [report.json]           Play with a 10 s warm-up and 30 s capture, write a JSON report and exit.
                                  Exit status 3 means the crowd missed 95% of its decision target.
  record [output-dir]             X11 only: record the pedestrian view to campus-crowd-demo.mp4.
  video [output.mp4]              X11 only: render the 20 s, 1080p camera-flight video.
  test                            Run the background inference and ScholaMass automation tests.
  create-level                    Regenerate the NorthCampus map from Content/Campus.
  help                            Show this message.

Game arguments (for run, measure and record):
  -Agents=N            crowd size, 1-100,000 (default 2,000)
  -DecisionHz=F        decisions per student per second (default 10)
  -Workers=N           inference worker threads (default 2)
  -MaxBatch=N          rows per model call (default 64)
  -MaxPendingRows=N    queue limit (default: about 50 ms of target decisions)
  -MaxRowsPerFrame=N   rows submitted per frame (default: half the queue)
  -MaxFPS=F            frame cap (default 60)
  -View=0..3           starting camera (3 is the flight); -FlightSeconds=S sets its length

Examples:
  ./demo.sh -Agents=20000
  ./demo.sh measure Saved/run.json -Agents=50000
USAGE
}

fail() {
	echo "demo.sh: $*" >&2
	exit 1
}

# Engine paths for this platform.
find_engine() {
	ENGINE="${UE_ENGINE_ROOT:-}"
	[[ -n "$ENGINE" && -d "$ENGINE/Engine" ]] || fail "set UE_ENGINE_ROOT to an Unreal Engine 5.8 installation (the directory containing Engine/)."
	case "$(uname -s)" in
		Linux)
			PLATFORM=Linux
			EDITOR="$ENGINE/Engine/Binaries/Linux/UnrealEditor"
			EDITOR_CMD="$ENGINE/Engine/Binaries/Linux/UnrealEditor-Cmd"
			;;
		Darwin)
			PLATFORM=Mac
			EDITOR="$ENGINE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
			EDITOR_CMD="$ENGINE/Engine/Binaries/Mac/UnrealEditor-Cmd"
			;;
		*) fail "unsupported platform $(uname -s); open $NAME.uproject in Unreal Editor instead." ;;
	esac
	BUILD="$ENGINE/Engine/Build/BatchFiles/$PLATFORM/Build.sh"
	[[ -x "$BUILD" ]] || fail "$BUILD not found; check UE_ENGINE_ROOT."
}

# Makes Plugins/Schola a view of this repository's plugin. Linking the repository
# root itself would recurse through Examples/*/Plugins/Schola, so link its parts.
link_plugin() {
	local Plugin="$PROJECT_DIR/Plugins/Schola"
	mkdir -p "$PROJECT_DIR/Plugins"
	[[ -L "$Plugin" ]] && rm "$Plugin"
	mkdir -p "$Plugin"
	# UnrealBuildTool needs the descriptor as a file inside the plugin directory.
	rm -f "$Plugin/Schola.uplugin"
	cp "$REPO_ROOT/Schola.uplugin" "$Plugin/Schola.uplugin"
	local Part
	for Part in Source Config Content Resources Shaders Binaries Intermediate; do
		[[ -e "$REPO_ROOT/$Part" || "$Part" == Binaries || "$Part" == Intermediate ]] || continue
		mkdir -p "$REPO_ROOT/$Part"
		[[ -e "$Plugin/$Part" || -L "$Plugin/$Part" ]] || ln -s "../../../../$Part" "$Plugin/$Part"
	done
}

build() {
	"$BUILD" "${NAME}Editor" "$PLATFORM" Development -Project="$PROJECT" -NoHotReloadFromIDE -NoUBA "$@"
}

# Starts the game in a 1600x900 window unless the arguments set a resolution.
# Uses the editor build, or the package with --packaged.
play() {
	local Packaged=0 Build=1
	while [[ $# -gt 0 ]]; do
		case "$1" in
			--packaged) Packaged=1 ;;
			--no-build) Build=0 ;;
			*) break ;;
		esac
		shift
	done
	local Args=(-windowed -nosound) HasX=0 HasY=0 Arg
	for Arg in "$@"; do
		case "$Arg" in -ResX=* | -resx=*) HasX=1 ;; -ResY=* | -resy=*) HasY=1 ;; esac
	done
	[[ "$HasX" == 1 ]] || Args+=(-ResX=1600)
	[[ "$HasY" == 1 ]] || Args+=(-ResY=900)
	Args+=("$@")
	if [[ "$Packaged" == 1 ]]; then
		[[ -x "$PACKAGED_GAME" ]] || fail "no package at $PACKAGED_GAME; run './demo.sh package' first."
		exec "$PACKAGED_GAME" "${Args[@]}"
	fi
	[[ "$Build" == 0 ]] || build
	exec "$EDITOR" "$PROJECT" -game "${Args[@]}"
}

# Absolute path without requiring the file to exist (realpath -m is GNU-only).
abspath() {
	case "$1" in
		/*) echo "$1" ;;
		*) echo "$PWD/$1" ;;
	esac
}

need_tools() {
	local Tool
	for Tool in "$@"; do
		command -v "$Tool" >/dev/null || fail "$Tool is required for this command."
	done
}

record() {
	need_tools ffmpeg xdotool grep
	[[ -n "${DISPLAY:-}" ]] || fail "recording needs an X11 display."
	local Out
	Out="$(abspath "${1:-$PROJECT_DIR/Saved/Recording}")"
	[[ $# -gt 0 ]] && shift
	mkdir -p "$Out"
	local Log="$Out/demo.log"
	: >"$Log"
	build
	# Caller arguments come first so they override these defaults.
	(play --no-build "$@" -View=2 -Agents=2000 -WarmupSeconds=12 -CaptureSeconds=12 -Report="$Out/run.json" \
		-Screenshot="$Out/pedestrians.png" -abslog="$Log" -unattended) >"$Out/launcher.log" 2>&1 &
	local Pid=$! Ready=0 I
	trap 'kill "$Pid" 2>/dev/null || true' EXIT
	for ((I = 0; I < 90; I++)); do
		if grep -q 'Tracing Screenshot.*pedestrians' "$Log" 2>/dev/null; then
			Ready=1
			break
		fi
		kill -0 "$Pid" 2>/dev/null || fail "the game exited; see $Log"
		sleep 1
	done
	[[ "$Ready" == 1 ]] || fail "timed out waiting for the scene; see $Log"
	local Window
	Window="$(xdotool search --onlyvisible --name "^$NAME " | head -n1)"
	ffmpeg -hide_banner -loglevel error -y -f x11grab -framerate 30 -draw_mouse 0 \
		-window_id "$Window" -i "$DISPLAY" -t 8 -vf scale=1280:720 \
		-c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p -movflags +faststart "$Out/campus-crowd-demo.mp4"
	wait "$Pid"
	trap - EXIT
	echo "$Out/campus-crowd-demo.mp4"
}

COMMAND="${1:-run}"
case "$COMMAND" in
	-*) COMMAND=run ;; # Game arguments only: play.
	*) [[ $# -gt 0 ]] && shift ;;
esac

case "$COMMAND" in
	help | -h | --help)
		usage
		exit 0
		;;
esac
find_engine
link_plugin

case "$COMMAND" in
	run) play "$@" ;;
	build) build "$@" ;;
	editor)
		build
		exec "$EDITOR" "$PROJECT" "$@"
		;;
	package)
		[[ "$PLATFORM" == Linux ]] || fail "packaging is scripted for Linux only."
		exec "$ENGINE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$PROJECT" -noP4 \
			-platform=Linux -clientconfig=Development -build -cook -stage -pak -archive \
			-archivedirectory="$PACKAGE_DIR" -map="$MAP" -unattended -utf8output -ubtargs="-NoUBA" "$@"
		;;
	measure)
		Report="$(abspath "${1:-$PROJECT_DIR/Saved/Measure/report.json}")"
		[[ $# -gt 0 ]] && shift
		# Unreal takes the first value of each argument, so the caller's settings win over these defaults.
		play "$@" -WarmupSeconds=10 -CaptureSeconds=30 -Report="$Report"
		;;
	record) record "$@" ;;
	video)
		need_tools python3 ffmpeg ffprobe xdotool fc-match
		build
		exec python3 "$PROJECT_DIR/Tools/render_demo_video.py" --project-dir "$PROJECT_DIR" \
			--output "$(abspath "${1:-$PROJECT_DIR/Evidence/campus-crowd-background-20s.mp4}")"
		;;
	test)
		build
		exec "$EDITOR_CMD" "$PROJECT" -nullrhi -unattended -nosound -NoSplash \
			"-ExecCmds=Automation RunTests Schola.Inference.Background+Schola.Mass;Quit" \
			"-ReportExportPath=$PROJECT_DIR/Saved/Tests" "$@"
		;;
	create-level)
		exec "$EDITOR_CMD" "$PROJECT" -run=pythonscript -script="$PROJECT_DIR/Tools/create_level.py" \
			-unattended -nullrhi -nosound -nop4 -NoSplash "$@"
		;;
	*)
		usage >&2
		fail "unknown command '$COMMAND'."
		;;
esac
