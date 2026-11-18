#!/usr/bin/env bash
# Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#
# Single entry point for the background NPC comparison demo.
#
#   ./demo.sh                     build (incremental) and play
#   ./demo.sh help                list every command
#
# Requires UE_ENGINE_ROOT to point at an Unreal Engine 5.8 installation.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"
NAME="BackgroundNPCs"
PROJECT="$PROJECT_DIR/$NAME.uproject"
PACKAGE_DIR="$PROJECT_DIR/Saved/Package"
PACKAGED_GAME="$PACKAGE_DIR/Linux/$NAME.sh"
MAP="/Engine/Maps/Entry"

usage() {
	cat <<USAGE
Background NPC comparison: the same Mass crowd run without Schola's background NPC API
(per-agent synchronous inference on the game thread) and with it. Press Tab in game to switch.

Usage: ./demo.sh [command] [options] [game arguments...]

Commands:
  run [--packaged] [--no-build]   Build the editor target and play (default command).
                                  --packaged plays the package from './demo.sh package' instead.
  build                           Build the editor target.
  editor                          Build, then open the project in Unreal Editor.
  package                         Build, cook and stage a standalone game in Saved/Package.
  compare [output-dir]            Linux, needs the package: measure both modes (10 s warm-up and
                                  30 s capture each) and write summary.md.
  record [output-dir]             Linux X11, needs the package: record both modes side by side.
  test                            Run the background inference and ScholaMass automation tests.
  help                            Show this message.

Game arguments (for run, compare and record):
  -Mode=Sync|Background   inference path at startup (default Background)
  -Agents=N               number of Mass entities (default 20,000)
  -DecisionHz=F           decisions per agent per second (default 10)
  -MaxFPS=F               frame cap, 0 for uncapped (default 60)
  -Workers=N -MaxBatch=N  background scheduler settings (default 2 and 64)
  -CaptureSeconds=S -WarmupSeconds=S -Report=file.json   measure, write a JSON report and exit

Examples:
  ./demo.sh -Mode=Sync -Agents=5000
  ./demo.sh package && ./demo.sh compare
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
		packaged_game
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

packaged_game() {
	[[ "$PLATFORM" == Linux ]] || fail "the packaged game is scripted for Linux only."
	[[ -x "$PACKAGED_GAME" ]] || fail "no package at $PACKAGED_GAME; run './demo.sh package' first."
}

compare() {
	packaged_game
	need_tools python3
	local Out Mode Lower
	Out="$(abspath "${1:-$PROJECT_DIR/Saved/Comparison}")"
	[[ $# -gt 0 ]] && shift
	mkdir -p "$Out"
	# The modes run one after another in separate processes, so they don't compete for the CPU.
	for Mode in Sync Background; do
		echo "Measuring $Mode..."
		Lower="$(echo "$Mode" | tr '[:upper:]' '[:lower:]')"
		"$PACKAGED_GAME" -windowed -ResX=1280 -ResY=720 -nosound -Mode="$Mode" -WarmupSeconds=10 -CaptureSeconds=30 \
			-Report="$Out/$Lower.json" -abslog="$Out/$Lower.log" "$@" >/dev/null
	done
	python3 "$PROJECT_DIR/Tools/summarize.py" "$Out/sync.json" "$Out/background.json" | tee "$Out/summary.md"
}

record() {
	packaged_game
	need_tools ffmpeg xdotool
	[[ -n "${DISPLAY:-}" ]] || fail "recording needs an X11 display."
	local Out Mode Lower Window Pid
	Out="$(abspath "${1:-$PROJECT_DIR/Saved/Recording}")"
	[[ $# -gt 0 ]] && shift
	mkdir -p "$Out"
	for Mode in Sync Background; do
		echo "Recording $Mode..."
		Lower="$(echo "$Mode" | tr '[:upper:]' '[:lower:]')"
		"$PACKAGED_GAME" -windowed -WinX=0 -WinY=0 -ResX=1280 -ResY=720 -nosound -Mode="$Mode" "$@" >/dev/null &
		# The launcher script runs the game as a child process; find its window by title.
		Window="$(xdotool search --sync --onlyvisible --name "^$NAME " | head -1)"
		Pid="$(xdotool getwindowpid "$Window")"
		sleep 12 # Let the crowd spread out and the HUD's one-second averages settle.
		ffmpeg -loglevel error -y -f x11grab -framerate 60 -window_id "$Window" -i "$DISPLAY" -t 30 \
			-c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p "$Out/$Lower.mp4"
		kill "$Pid"
		wait 2>/dev/null || true
	done
	ffmpeg -loglevel error -y -i "$Out/sync.mp4" -i "$Out/background.mp4" \
		-filter_complex "[0:v]scale=1280:720,setsar=1[a];[1:v]scale=1280:720,setsar=1[b];[a][b]hstack" \
		-c:v libx264 -preset slow -crf 30 -pix_fmt yuv420p -movflags +faststart "$Out/side-by-side.mp4"
	echo "$Out/side-by-side.mp4"
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
	compare) compare "$@" ;;
	record) record "$@" ;;
	test)
		build
		exec "$EDITOR_CMD" "$PROJECT" -nullrhi -unattended -nosound -NoSplash \
			"-ExecCmds=Automation RunTests Schola.Inference.Background+Schola.Mass;Quit" \
			"-ReportExportPath=$PROJECT_DIR/Saved/Tests" "$@"
		;;
	*)
		usage >&2
		fail "unknown command '$COMMAND'."
		;;
esac
