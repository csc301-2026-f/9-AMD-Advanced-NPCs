"""Print a Markdown comparison table from two capture reports.

Usage: python3 summarize.py sync.json background.json
"""
import json
import sys

sync, background = (json.load(open(path)) for path in sys.argv[1:3])

ROWS = [
    ("Frame rate (FPS)", "fps", "{:.1f}"),
    ("Mean frame time (ms)", "frame_ms_mean", "{:.1f}"),
    ("p99 frame time (ms)", "frame_ms_p99", "{:.1f}"),
    ("Game-thread inference per frame (ms)", "inference_game_thread_ms_mean", "{:.2f}"),
    ("Decisions per second", "decisions_per_second", "{:,.0f}"),
    ("Share of target decisions delivered", "target_fraction", "{:.0%}"),
    ("Agents per model call", "rows_per_model_call", "{:.1f}"),
    ("Inference time per second of play (ms)", "model_ms_per_second", "{:,.0f}"),
    ("Observation-to-action latency (ms)", "action_latency_ms_mean", "{:.1f}"),
    ("Dropped requests", "dropped_rows", "{:,.0f}"),
]

print(f"{sync['agents']:,} Mass agents, {sync['decision_hz_per_agent']:g} decisions/s each "
      f"(target {sync['target_decisions_per_second']:,.0f}/s), frame cap {sync['max_fps']:g} FPS, "
      f"{sync['capture_seconds']:.0f} s capture after warm-up. CPU: {sync['cpu']}; GPU: {sync['gpu']}.")
print()
print("| Metric | Without API (per-agent sync) | With background NPC API |")
print("| --- | ---: | ---: |")
for label, key, fmt in ROWS:
    cells = [fmt.format(report[key]) if key in report else "n/a" for report in (sync, background)]
    print(f"| {label} | {cells[0]} | {cells[1]} |")
for name, report in (("Without API", sync), ("With background NPC API", background)):
    if report.get("target_met") is False:
        print(f"\n**{name} missed the target decision rate:** {report.get('shortfall', 'see report')}.")
