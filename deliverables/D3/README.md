# Deliverable 3: Advanced NPCs for AMD Schola

- [Slideshow](index.html)
- [Product evolution report](product-evolution.md)
- [Individual contributions](Individual_Contributions_Deliverable3.md)
- [Adaptive NPC workflow, test results and NFR evidence](adaptive-npc-evidence.md)

The adaptive NPC section covers the reusable native Schola/RLtools integration.
The existing Shared Policy slides and report content remain owned by that subteam.

## Open and present the slideshow

Open **index.html** in a modern browser. The slides work offline with no server,
external fonts or installed packages. Keep this folder and its relative paths
within the repository when following evidence links.

- Arrow keys, Page Up / Page Down, or the on-screen buttons change slides.
- Space / Shift + Space changes slides when a button or link is not focused.
- **O** opens the overview. **Home / End** selects the first / last slide.
- **N** toggles speaker notes. Notes appear in the shared browser window; close
  them before showing the audience the slide.
- **F** requests fullscreen. **?** shows shortcuts. **Escape** closes dialogs/notes.
- Browser Print / Save as PDF exports one 16:9 page per slide. Enable background
  graphics and disable browser headers/footers. Notes and controls do not print.

The adaptive slides use these stable links:

| Slide | Purpose |
| --- | --- |
| [Opening](index.html#title) | Native learning through Schola |
| [D2 baseline](index.html#baseline) | Task-specific prototypes and the reuse gap |
| [Gap](index.html#gap) | Developer need and observable acceptance criterion |
| [Workflow](index.html#improvement) | Configure, connect gameplay and observe learning |
| [Architecture](index.html#architecture) | Game, Schola and RLtools responsibilities |
| [Before / after](index.html#comparison) | Concrete capability changes |
| [Live game demo](index.html#demo) | Handoff to the team's prepared game |
| [Validation](index.html#validation) | UE 5.8.4 results and their limits |
| [Non-functional requirements](index.html#nfr) | Reliability, resources, isolation and evidence |
| [Process / CI/CD](index.html#process) | Reviewed PRs, repeatable checks and pending AMD integration |
| [D4 scope](index.html#d4) | Automatic collection, evaluation, persistence and delivery |
| [Discussion](index.html#questions) | Questions about the adaptive section |
| [References](index.html#references) | Repository artifacts and implementation PRs |

The Shared Policy section follows these slides, starting at
[index.html#shared-policy-team](index.html#shared-policy-team). Its content and
layout are preserved. Use the overview to move between subteams.

## Adaptive developer workflow

The game defines observations, actions, rewards and episode conditions. Schola
connects those concepts to native SAC, TD3 or PPO, which learns inside Unreal.
This parallels Schola's Python integration with SB3/RLlib while using C++ locally.

1. Configure the native policy and supported spaces/settings in Blueprint or C++.
2. Initialize with a Schola interaction definition and wait for **Ready**.
3. Request a decision and execute its returned action.
4. Submit the attributed reward, next observation and episode outcome.
5. Inspect accepted experiences, completed updates and a newer policy version.

Custom shapes and supported network settings come from a configuration asset.
Export the project request, rebuild the C++ project and assign the asset to the
policy. The [configuration guide](../../Docs/Sphinx/guides/rltools_configuration_generation.rst)
explains the settings and build requirements.

For reproducible headless verification on Windows with **UE 5.8.4**, Visual Studio
2022 Game development with C++ and a Windows SDK, run from the repository root:

```powershell
./Examples/NativeLearning/Test-NativeLearning.ps1 -EnginePath 'D:\Apps\Epic Games\UE_5.8'
./Test/rltools_generation/Run.ps1 -EnginePath 'D:\Apps\Epic Games\UE_5.8'
```

Expected results: **40 successful Unreal tests** and **43 generator checks**.
First-time compilation can take several minutes. No Python training service is
required. See the [evidence record](adaptive-npc-evidence.md) for outputs, exact
scope, test names and [machine-readable results](evidence/adaptive-unreal-summary.json).

## Live-game demo and rehearsal

The team will show a game during the presentation; the demo slide provides its
handoff and the observations to explain. The game's presenter should rehearse
its launch and starting state, then show the task, scored gameplay outcomes and
learning progress. Name the implementation the game actually uses. A D2 prototype
is valid evidence for its own gameplay, while the D3 policy tests establish the
reusable integration. A changed policy version alone does not prove improved
behavior.

Suggested adaptive slide timings total about **five minutes**, including a
one-minute game handoff, before discussion. These are rehearsal budgets, not a
course-imposed duration; fit this section to the full team's allotted slot. Keep
the already-built NativeLearning host and saved results ready as an API fallback
if the game cannot run. A recording or game command is not fabricated here.

Before presenting, confirm the actual game build and controls, the presenter
handoff, the available time, and a genuine gameplay backup. Check fullscreen and
links on the presentation machine and keep notes closed during screen sharing.
The report and slide copy are complete for the implemented adaptive integration;
the game presentation is a separate team-owned activity.

## Engineering process and non-functional requirements

The [NFR record](adaptive-npc-evidence.md#non-functional-requirements) maps resource
bounds, responsive background execution, decision integrity, world isolation,
reliable teardown and developer access to actual tests. Supported scope is CPU
feedforward networks, Box observations and bounded continuous Box actions.
Execution limits are cooperative, not hard real-time guarantees.

**AMD has its own CI/CD checks. Those checks have not been integrated into this
codebase yet; we are working with the partner to get that setup.** Current
verification uses repeatable local scripts and teammate-reviewed PRs. This
outstanding dependency is visible in the dedicated process slide and report;
local results are not labelled as CI results.

## D3 submission boundary

The adaptive materials document implemented capabilities and their limits.
Automatic task collection, resumable checkpoints and separate-project packaged
validation remain follow-ups. Other subteams own their report, contribution and
slide content. The final team submission still needs the required `D3` release/tag
and each member's ICM1 submission; this folder does not imply those actions have
already happened.
