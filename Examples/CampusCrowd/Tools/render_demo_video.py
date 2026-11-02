"""Capture a 1080p Schola background-inference flight over the campus from Unreal's X11 window."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def run(args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--agents', type=int, default=20000)
    parser.add_argument('--decision-hz', type=float, default=10)
    parser.add_argument('--max-batch', type=int, default=64)
    parser.add_argument('--workers', type=int, default=2)
    parser.add_argument('--seconds', type=float, default=20)
    args = parser.parse_args()
    project = args.project_dir.resolve()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    work = project/'Saved/BackgroundVideo'
    work.mkdir(parents=True, exist_ok=True)
    log = work/'game.log'
    log.write_text('')
    launcher_log = open(work/'launcher.log', 'w')
    game = subprocess.Popen([
        str(project/'Scripts/run.sh'), f'-Agents={args.agents}', '-Mode=Background', '-View=2', f'-FlightSeconds={args.seconds:g}',
        f'-DecisionHz={args.decision_hz}', f'-MaxBatch={args.max_batch}', f'-Workers={args.workers}',
        '-ResX=1920', '-ResY=1080', '-ForceRes', '-NoBorder', '-WinX=0', '-WinY=0',
        '-MaxFPS=60', '-WarmupSeconds=10', f'-Screenshot={work / "ready.png"}',
        f'-abslog={log}', '-unattended',
    ], stdout=launcher_log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic()+120
        while time.monotonic()<deadline:
            if game.poll() is not None:
                raise RuntimeError(f'Unreal exited with {game.returncode}; see {log}')
            if 'Tracing Screenshot "ready"' in log.read_text(errors='replace'):
                break
            time.sleep(1)
        else:
            raise RuntimeError(f'Timed out waiting for the campus; see {log}')
        time.sleep(2)
        window = run(['xdotool','search','--onlyvisible','--pid',str(game.pid),'--name','^CampusCrowd '], capture_output=True, text=True).stdout.splitlines()[0]
        run(['xdotool','windowfocus','--sync',window])
        # Start the camera flight immediately before the capture. Synthetic key
        # presses are occasionally dropped, so retry until the game logs the start.
        flights = log.read_text(errors='replace').count('Camera flight started')
        for _ in range(10):
            run(['xdotool','windowactivate','--sync',window])
            run(['xdotool','keydown','4']); time.sleep(.15); run(['xdotool','keyup','4'])
            retry = time.monotonic()+1.5
            while time.monotonic()<retry and log.read_text(errors='replace').count('Camera flight started')==flights:
                time.sleep(.05)
            if log.read_text(errors='replace').count('Camera flight started')>flights:
                break
        else:
            raise RuntimeError(f'The game did not start the camera flight; see {log}')
        print(f'Capturing {args.seconds:g}s', flush=True)
        clip = work/'background.mp4'
        with open(work/'capture.log','w') as capture_log:
            run(['ffmpeg','-hide_banner','-loglevel','error','-y',
                 '-thread_queue_size','512','-f','x11grab','-framerate','60','-draw_mouse','0',
                 '-window_id',window,'-i',os.environ['DISPLAY'],'-t',f'{args.seconds:g}',
                 '-c:v','libx264','-preset','veryfast','-crf','18','-threads','2',
                 '-pix_fmt','yuv420p',str(clip)],stdout=capture_log,stderr=subprocess.STDOUT)
        font = run(['fc-match','-f','%{file}','sans:charset=2192 2264 b7'],capture_output=True,text=True).stdout
        bold = run(['fc-match','-f','%{file}','sans:style=Bold:charset=2192 2264 b7'],capture_output=True,text=True).stdout
        # Captions go through text files so apostrophes, commas and colons need no filter escaping.
        def text(name, value, y, size, color, fontfile=font):
            path = work/f'caption-{name}.txt'
            path.write_text(value)
            return (f"drawtext=fontfile='{fontfile}':textfile='{path}':expansion=none:"
                    f"x=w-824:y={y}:fontsize={size}:fontcolor={color}")
        workers = 'worker' if args.workers==1 else 'workers'
        filters = [
            'drawbox=x=iw-848:y=28:w=820:h=136:color=black@0.72:t=fill',
            text('title','SCHOLA BACKGROUND INFERENCE',44,28,'white',bold),
            text('load',f'{args.agents:,} NPCs · {args.decision_hz:g} decisions/s each · batches of ≤{args.max_batch} on {args.workers} {workers}',90,20,'0xD1DEDF'),
            text('model','MLP 64→256→256→2, FP32, untrained · ONNX Runtime CPU via Unreal NNE',124,20,'0xD1DEDF'),
        ]
        print('Encoding the 1080p showcase', flush=True)
        with open(work/'encode.log','w') as encode_log:
            run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(clip),
                 '-vf',','.join(filters),'-c:v','libx264','-preset','medium','-crf','18','-threads','4',
                 '-pix_fmt','yuv420p','-movflags','+faststart',str(output)],stdout=encode_log,stderr=subprocess.STDOUT)
        probe = json.loads(run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(output)],capture_output=True,text=True).stdout)
        stream = next(s for s in probe['streams'] if s['codec_type']=='video')
        if (stream['width'],stream['height'])!=(1920,1080):
            raise RuntimeError('Unexpected capture resolution')
        if abs(float(probe['format']['duration'])-args.seconds)>0.2:
            raise RuntimeError('Unexpected showcase duration')
        metadata={'file':output.name,'width':1920,'height':1080,'fps':stream['avg_frame_rate'],
                  'duration_seconds':float(probe['format']['duration']),'codec':stream['codec_name'],
                  'source':'Live Unreal Engine 5.8.3 gameplay, captured from its X11 window',
                  'agents':args.agents,'target_decision_hz_per_agent':args.decision_hz,
                  'max_batch':args.max_batch,'workers':args.workers,'max_game_fps':60,
                  'mode':'background',
                  'camera':f'{args.seconds:g}-second flight from 22 m over the front lawn, across Queen\'s Park, to an overview of the whole campus model; real-time playback. Distant students are enlarged to stay at least 3 px wide'}
        output.with_suffix('.json').write_text(json.dumps(metadata,indent=2)+'\n')
        print(str(output),flush=True)
    finally:
        if game.poll() is None:
            game.terminate()
            try:game.wait(timeout=15)
            except subprocess.TimeoutExpired:game.kill();game.wait()
        launcher_log.close()


if __name__=='__main__':main()
