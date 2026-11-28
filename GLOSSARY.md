# Advanced NPCs (Schola native adaptive learning)

Terms specific to migrating chase-style online-learning experiments from the
example-owned SAC prototype onto the Schola plugin's native backend
(`ScholaRLTools` / `URLToolsNPCPolicy`).

## Language

**Profile**:
A pre-compiled RLtools configuration (observation count, action count, algorithm,
network/batch/replay settings) that `URLToolsNPCPolicy` must resolve before it can
construct a learner. Declared in a project's `Config/ScholaRLTools.json` and
compiled by the generator commandlet; `Init` fails if no matching profile exists.
_Avoid_: shape, configuration (alone, without "profile")

**Driving loop**:
Experiment-scoped glue code that calls `RequestDecision` → `TryGetDecision` →
apply → `SubmitExperience` each tick against `URLToolsNPCPolicy`. Written
per-experiment because no reusable Schola stepper for this route exists yet
(`UAdaptiveStepper`/`IAdaptiveAgent` are unbuilt, separate team work). Not a
candidate for generalization during this migration.
_Avoid_: stepper, coordinator (those name the future reusable thing, not this)

**Frozen**:
In the plugin, a `SubmitExperience` rejection reason returned when
`SetLearningEnabled(false)` is active — admission is paused, nothing else.
It does **not** affect whether actions are sampled or deterministic; that is
`SetInferenceMode`. (The old prototype's `ChaseSession::SetFrozen` combined both
effects in one flag — that combined behavior does not exist in the plugin and
must be reproduced by setting `SetLearningEnabled(false)` and
`SetInferenceMode(Deterministic)` together.)
_Avoid_: using "frozen" to mean deterministic inference alone

**Chase task**:
The pursuit scenario (chaser NPC vs. a scripted runner) used to exercise the
learner. Its specific observation layout, reward shaping, and visuals are
disposable test material, not a fixed contract — they may be redesigned for
the Unreal migration as long as the same kind of adaptation claim (learns,
then recovers after the runner's behavior changes) is being tested.
_Avoid_: treating ChaseWorld's exact fields as something that must be ported
