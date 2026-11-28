# Advanced NPCs for AMD Schola - Individual Contributions (D4)

Each entry records that member's D4 work. Subteams maintain their own entries.

### Chloe Lee — Adaptive NPCs: checkpoints in the chase test and evaluation

- The chase adaptation test can now start from a saved checkpoint
  ([#77](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/77), reopened from
  [#71](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/71)). It loads one file into two
  learners, one that keeps learning and one frozen, so both start from the same weights (test 9).
  The test also checks that the frozen one never learns anything.
- Ran it on six seeds ([#74](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/74)). All 12
  runs pass. Loading the file gives back the same catch rates, and the learner recovers in 2,774 to
  13,994 moves.
- Added decision time, feedback counts and fallbacks to the test output
  ([#72](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/72)). A decision takes about 4.7 µs.
- Native learner CI now runs on macOS too, and I fixed a test that could time out on Linux
  ([#70](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/70)).
- D4 slides: team intro slides, three checkpoint slides and the testing slide
  ([#78](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/78),
  [#81](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/81)).

**Evidence:** [#70](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/70),
[#72](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/72),
[#74](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/74),
[#77](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/77),
[#78](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/78),
[#81](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/81)

### Olivia — Adaptive NPCs: playable chase demo in Unreal

- Built a playable chase demo in `Examples/NativeLearning` ([#88](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/88)). A chaser NPC learns to catch a
  runner while the game runs, using the native SAC learner (`URLToolsNPCPolicy`) on a new
  `chase-demo-sac` profile. The runner's six habits are ported from the D3 terminal simulator in
  `Examples/AdaptiveTag`.
- The demo can be driven live: switch the runner's habit, take control of the runner, freeze
  learning, or speed up the game. A HUD shows episodes, the rolling catch rate, gradient updates and
  learning state, so you can watch the catch rate drop after a switch and then recover.
- Wrote the README section on running and tuning the demo, and ADRs 0001/0002 on moving SAC into
  the plugin, plus a project glossary.
- D4 slides: added the Unreal chase demo and simplified the adaptive slides. Merged the adaptive
  evidence and scaling into one slide with the Unreal results, and noted that the Unreal chase test
  passed on Windows ([#82](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/82)).

**Evidence:** [#82](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/82),
[#88](https://github.com/csc301-2026-f/9-AMD-Advanced-NPCs/pull/88)
