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
