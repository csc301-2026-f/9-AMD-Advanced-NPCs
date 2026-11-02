# North Campus crowd demo

A standalone Unreal Engine 5.8.3 project built from the repository's `50H_NORTH.dwg`, using Schola's background inference API and Mass entities for the student crowd.

```bash
cd Examples/CampusCrowd
Scripts/run.sh                  # play: 2,000 students, background inference
Scripts/editor.sh               # open NorthCampus in Unreal Editor; press Play
Scripts/run.sh -Agents=5000 -Mode=Sync
```

The scripts default to `/home/future/Downloads/UnrealEngine-5.8.3`. Set `UE_ENGINE_ROOT` to use another installation. The editor module has been built locally. On a fresh checkout, run `Scripts/build.sh` before launching. The scripts link Schola's individual plugin directories under `Plugins/Schola`, avoiding recursive links through the example projects. Linux is the validated platform.

| Control | Action |
| --- | --- |
| Tab | Switch between Schola background batching and per-agent synchronous inference |
| 1 / 2 / 3 | Campus overview / University College / pedestrian camera |
| 4 | Restart the camera flight from the front lawn to a full-campus overview |
| WASD, Q / E | Fly forward/sideways, down/up |
| Right mouse + drag | Look around |
| Shift | Fly faster |
| R | Reset overview camera |
| Space | Pause/resume crowd movement; inference continues for inspection |
| N | Show/hide the pedestrian route graph |

The HUD reports FPS, frame time and p99, game-thread inference cost, completed decisions, batch size, worker model time, action latency, and dropped requests. Both modes use the same scene, population, ONNX model, route following, separation and rendering code.

## Campus geometry

The campus crop preserves the supplied CAD **3D polyface and planar ACIS REGION massings and their heights**, plus `Curbs` and `Parks` surface meshes. This is a massing visualization, with simple materials; it does not reconstruct facade details absent from the drawing. It contains 340 connected building footprints and about 689,000 triangles. Decorative trees and the student cylinders are procedural demo additions.

The CAD uses metres. The crop spans 1,300 × 1,400 metres around CAD origin `(313210, 4835890)`. Unreal positions are centimetres: `X=(CAD X−313210)×100`, `Y=−(CAD Y−4835890)×100`, `Z=CAD Z×100`. See [the import report](Content/Campus/import-report.json) for the source SHA-256, bounds, mesh counts and coordinate transform, and [the campus plan](Content/Campus/campus-plan.png) for the geometry and route overlay. The crop covers the university and nearby blocks within the supplied north drawing; it does not import every off-campus survey object.

`Content/Maps/NorthCampus.umap` contains a native `CampusScene` actor. Its procedural mesh is regenerated from the bundled `Content/Campus/campus.json` when the level opens, so the map stays small. Materials are native Unreal assets. You can add actors and edit the level normally. Change the importer to alter the CAD crop or generation rules.

## Crowd behavior and inference

Students follow a medial route network derived from the CAD's paved and park surfaces, with route edges checked against buffered building footprints. They vary their walking speed, choose new branches at junctions, make short stops, and use a spatial grid for nearby-agent separation. Each student is a single instanced cylinder, 60 cm wide and 180 cm tall. Independent precincts remain separated by roads; there is no simulated traffic or crossing controller.

The 4,135 route nodes are demo navigation derived from free surfaces, **not surveyed footpaths or entrances**. The ONNX model supplies a bounded steering contribution; route constraints and local separation provide navigation. This reuses the comparison example's hand-built, untrained steering network, not a learned student-behavior policy. The demo exercises real Schola gather/apply processors, worker batching, NNE ORT inference, and asynchronous results.

Default arguments are `-Agents=2000 -DecisionHz=10 -Workers=2 -MaxBatch=64 -Mode=Background -MaxFPS=60`. `-View=0|1|2|3` selects an initial camera. Larger crowds are available with `-Agents=N` (up to 100,000); performance depends on the machine and inference mode.

## CAD import coverage and Datasmith

[Datasmith CAD Importer](https://dev.epicgames.com/documentation/unreal-engine/importing-cad-files-into-unreal-engine-using-datasmith) is the native CAD route to try first. On the installed Linux UE 5.8.3 build, direct tests of the original DWG, audited AutoCAD 2010 DWG and audited AutoCAD 2018 DXF all failed in both CADKernel and TechSoft, yielding zero mesh actors. See [the recorded probe results](Evidence/datasmith-probe.json). A true return from Datasmith's `translate_scene()` alone did not indicate usable imported geometry.

The fallback therefore reads the audited DXF's polyface meshes, 3DFACE triangles, and planar ACIS REGION faces. The initial mesh-only importer skipped the REGION faces, which omitted University College and other massings. The corrected importer decodes all 19,526 source massing regions, including AutoCAD's typed binary transform encoding. Concave boundaries and inner loops are triangulated without filling holes. Unreadable or unsupported regions abort generation rather than being silently omitted. Mesh/crop intersection also uses full bounding boxes so crossing meshes are retained.

The generated `entity_audit` records source and decoded region counts. [The University College preview](Evidence/university-college.png) shows the restored geometry; press **2** in the demo to inspect it. `Tools/tests/fixtures/university-college-roof.sab` is a small raw CAD region used as a regression fixture; it is not a replacement model. Run `python -m unittest discover -s Tools/tests -v` for the five decoder and triangulation tests. The source-CAD bounds around University College are checked separately by the geometry validator.

## Rebuild assets and measure

The generated campus data, materials, map and model are included. CAD conversion tools are only needed to regenerate geometry.

1. Convert the original DWG to ASCII DXF using [ODA File Converter](https://www.opendesign.com/guestfiles/oda_file_converter), output version ACAD2018, with audit enabled. LibreDWG 0.13.4 also decoded the drawing; its large JSON export required number sanitization, so the reproducible importer uses ODA's DXF instead.
2. Install the temporary importer dependencies: `python -m pip install -r Tools/requirements.txt`.
3. Run `python Tools/import_campus.py /path/to/50H_NORTH.dxf`.
4. Run `Scripts/build.sh`, then `Scripts/create-level.sh` to generate/update the Unreal materials and map. The level script preserves existing actors when updating an existing level.

For a short measurement and screenshot (the process exits after capture):

```bash
Scripts/run.sh -Mode=Background -Agents=2000 \
  -WarmupSeconds=15 -CaptureSeconds=10 \
  -Report="$PWD/Saved/background.json" \
  -Screenshot="$PWD/Saved/campus.png"
```

Repeat with `-Mode=Sync` and a separate report path. Screenshots are requested during warm-up, before measurement, to avoid including capture stalls in the report. Add `-CleanScreenshot` to hide the HUD in the capture. `Scripts/package.sh` builds/cooks a standalone Linux game under `Saved/Package`; the editor game launch is the tested path. The model and CAD data are staged as non-UFS files for runtime loading.

Run `python Tools/validate_campus.py` to recheck geometry indices, the source hash, graph connectivity, and the three-metre building clearance of every route segment. Validation results and in-engine captures are in `Evidence/`.

The included [measurement comparison](Evidence/comparison.md) recorded 60 FPS in both modes at 2,000 students on this machine. Game-thread inference averaged 1.83 ms with synchronous calls and 0.34 ms with background batching; both completed about 20,000 decisions/s with zero drops. These are short local measurements, not a general hardware benchmark. The nine existing SharedPolicy stepper automation tests also pass after correcting their enum conversions and intentional-error expectations.

`Scripts/record.sh` records an eight-second pedestrian-view clip under `Saved/Recording` using X11, `ffmpeg`, `xdotool` and `rg`. An in-engine clip and screenshots are included in `Evidence/`.

`Scripts/render-video.sh` records a 20-second, 1920×1080, 60 FPS clip of Schola background inference to `Evidence/campus-crowd-background-20s.mp4`: 20,000 students at 10 decisions/s each, maximum batch 64 on 2 workers. The camera flies from 22 m over the front lawn, across Queen's Park, to a view of the whole campus model, following wall time. Distant students are drawn larger so each stays at least 3 pixels wide; within about 370 m they keep their real 60 × 180 cm size. On this machine the capture held 60 FPS with about 200,000 decisions/s and no dropped requests. `-FlightSeconds=S` sets the flight length for the **4** camera (default 10).

The renderer launches the game on the current X11 display and captures its window. It requires `python3`, `ffmpeg`, `ffprobe`, `xdotool` and `fc-match`; an optional first argument sets the output path. The Python helper also accepts `--agents`, `--decision-hz`, `--max-batch`, `--workers` and `--seconds`.

The original [background NPC comparison](../BackgroundNPCs/README.md) remains a separate benchmark scene.
