# CampusCrowd D3 validation — 5 November 2026

Validated on Linux with Unreal Engine 5.8.3 (CL 58210709), Ryzen 5 7600X, and Radeon RX 7800 XT. Base commit: `58b0ccb`; the working tree also disables the unused Android file server and updates D3 documents. No C++ gameplay changes were made for validation. Unreal expanded default input settings while testing; the [tested settings](tested-DefaultInput.ini.gz) are retained, and the source file was restored to its original form afterward. [Manifest](manifest.json) records commands, exit codes, configuration hash, and package hashes.

| Check | Result | Evidence |
| --- | --- | --- |
| Editor build | Passed, exit 0; 96.77 seconds | [Build log](build.log) |
| Background inference + Mass automation | 43 passed, 0 failed; exit 0 | [Unreal JSON report](automation.json), [full log](automation.log.gz) |
| Build, cook, stage, archive | Passed, exit 0; 109.33 seconds | [Packaging log](package.log.gz) |
| Packaged 20,000-agent run | 59.98 FPS; 199,922 decisions/s; 99.96% of target; exit 0 | [Timed report](packaged-background.json), [screenshot](packaged-background.png), [log](packaged-background.log.gz) |
| Packaged controls | Camera views, inference-mode switch, pause/resume, normal close; exit 0 | [Pedestrian view](packaged-pedestrians.png), [synchronous mode](packaged-synchronous.png), [paused](packaged-paused.png), [resumed](packaged-resumed.png), [log](packaged-controls.log.gz) |

The timed run used 1600×900, a 60 FPS cap, 10 decisions/s per agent, two CPU workers, and batches of 64. After a 10-second warm-up, the 10-second capture recorded 600 frames, zero dropped/rejected/expired/failed/stale rows, 1.64 ms/frame of game-thread inference, and 15.81 ms mean action latency. A temporary throughput warning occurred during warm-up and recovered before capture. The overview screenshot was taken during warm-up; the JSON report establishes the measured result.

All 43 automation tests have `Success` state; three contain warnings: tensor-name fallback in NNE batch equivalence, synchronous inference in the shared-bucket reconfiguration test, and ignored shared-fragment composition in the Mass lifetime/migration test. These warnings remain in the report. An initial run also passed 43 tests but exited 1 because `-TestExit` forced termination. [Initial report](automation-initial.json) and [log](automation-initial.log.gz) are retained. The final command uses `Automation ...;Quit`, which exits 0 after the suite finishes.

The controls run used 2,000 agents and was separate from the performance capture. Click the game viewport before keyboard input. The pause screenshot shows 0.00 ms movement time; resuming restores movement. The HUD showed 333 cumulative dropped requests after the interactive checks; their cause was not isolated. This was not a zero-loss transition test. The route-overlay key was exercised, but its visibility was not verified.

The standalone package is in `Examples/CampusCrowd/Saved/Package/Linux` (generated and ignored by Git). Launch `CampusCrowd.sh` from that directory; distribute the entire `Linux` directory together. Package hashes are in the manifest; package binaries have not been published as a release asset. Decompress full logs with `gzip -dc <file>.log.gz`.

This is a local Linux check, not independent-machine validation or a cross-platform test. No new 50,000- or 100,000-agent benchmarks, full CAD source validation, SharePoint playback, or peer-review audit were performed.

Before committing, the editor-regenerated Android development token was cleared explicitly with `SecurityToken=`. The file server and network access remain disabled. The manifest’s configuration hash identifies the tested settings, before this token cleanup.
