Native adaptive policies
========================

Schola defines the connection between gameplay and an RL library. With Python
training, a game supplies observations, actions, rewards and episode boundaries;
Schola connects those values to an SB3 or RLlib algorithm. The native integration
uses the same concepts with RLtools inside the Unreal process.

``URLToolsNPCPolicy``, in ``ScholaRLTools``, is a concrete
``UAdaptiveNPCPolicy`` usable from Blueprint and C++. It operates the existing
native SAC, TD3 and PPO learner; game code does not own replay buffers, compute
PPO likelihoods or implement optimizers. Training continues on background workers
while gameplay selects actions.

Configure and initialize
------------------------

Create the policy with an outer belonging to a live game, PIE or game-preview
world, such as an actor or component. Keep it in a reflected reference for its
required lifetime. Each world has a ``URLToolsWorldSubsystem``.

1. Select ``Algorithm`` and call ``GetAvailableConfigurations`` for that
   algorithm. Copy a returned full identifier into ``Configuration``.
2. Set ``Settings``. Leave ``SharedGroup`` as None for independent learning.
3. Define Schola's ``FInteractionDefinition``: Box observations and bounded,
   finite, continuous Box actions, with dimensions matching the selected profile.
4. Call ``Init``. True means spaces/settings validated and construction accepted,
   not that numerical state is ready. New learners are initially **Locally
   Initialized**; joining an already-ready shared learner may complete immediately.
   Expensive learner construction happens on a worker.
5. Poll ``GetSessionState`` for Ready and ``GetInitializationOperation`` for
   completion. Failed construction reports Failed and ``GetLastError``. Do not
   busy-wait on the gameplay thread; check on later ticks or from your coordinator.

Invalid local replacements preserve a previously valid binding. Successful
reinitialization releases the previous binding and creates or joins a new one.
Shutdown during construction cancels initialization; a delayed completion cannot
restart a stopped or replaced binding. Duplicated/loaded configuration does not
inherit a live learner.

Available choices
-----------------

Profiles are discovered from ``FLearner::GetProfiles()``, not an independently
maintained list. The shipped profiles are:

.. list-table::
   :header-rows: 1

   * - Algorithm
     - Full identifiers
     - Observation/action dimensions
   * - SAC
     - ``sac/3x2-mlp32``, ``sac/5x1-mlp32``
     - 3/2, 5/1
   * - TD3
     - ``td3/3x2-mlp32``, ``td3/5x1-mlp32``
     - 3/2, 5/1
   * - PPO
     - ``ppo/3x2-mlp32``, ``ppo/5x1-mlp32``
     - 3/2, 5/1

The reflected, read-only profile description exposes network width/layers, batch
size, replay capacity or rollout length, discount, target update parameters,
SAC entropy choices, TD3 smoothing, PPO clipping/advantage/distribution choices,
device and dependency revision. These values are fixed by compilation. Selecting
an algorithm alone does not change a mismatched configuration identifier.

There are no editable controls claiming to alter those structural parameters.
An uncompiled identifier or dimension mismatch fails with configuration/build
guidance. Automatic arbitrary configuration generation is separate work.

Runtime settings
----------------

Settings are copied at initialization. Editing the configuration object later
does not mutate a live learner. ``GetActiveSettings`` returns the active
learner-wide choices, including a seed changed by a shared learning reset.

.. list-table::
   :header-rows: 1
   :widths: 32 18 50

   * - Setting
     - Applies to
     - Meaning
   * - ``Seed``, ``QueueCapacity``
     - All
     - Initialization/exploration seed and pending experience capacity.
   * - ``LearningRate``
     - All
     - Optimizer learning rate.
   * - ``MaxRecordsPerBurst``, ``MaxUpdatesPerBurst``
     - All
     - Bounded storage and SAC/TD3 cycles or PPO epochs per worker task.
   * - ``LearningStarts``
     - SAC/TD3
     - Warm-up records; must be at least the compiled batch size.
   * - ``InitialRandomSteps``
     - SAC/TD3
     - Initial random exploration decisions per agent.
   * - ``TrainFrequency``, ``GradientSteps``
     - SAC/TD3
     - Collection interval and training cycles per interval.
   * - ``PublishEveryUpdates``
     - SAC/TD3
     - Actor publication interval. PPO publishes after a complete rollout update.
   * - ``ExplorationNoise``
     - TD3
     - Exploration standard deviation. SAC/PPO use their learned distributions.
   * - ``PPOEpochs``
     - PPO
     - Total training epochs for a complete on-policy rollout.
   * - ``ObservationMean``, ``ObservationStd``
     - All
     - Fixed preprocessing: both empty for identity, otherwise one finite mean
       and positive standard deviation per input.

Algorithm-specific fields are identified in the reflected settings categories
and tooltips. Inapplicable fields do not enable missing algorithm functionality.
Running normalization is not implemented. Pass raw Schola observations; the
native learner applies fixed preprocessing once, and captured actors retain it.

Decisions and gameplay outcomes
-------------------------------

On later gameplay ticks:

1. Request a decision with the current Schola observation and a selection timeout.
2. Poll ``TryGetDecision``. Only Ready yields a usable command.
3. Execute the returned ``Action`` and retain its attribution.
4. When reward and next observation are available, submit an
   ``FAdaptiveExperience`` through the common policy API.
5. Inspect ``GetLearningStats`` for storage, training and a newer published version.

For example, after constructing observation/space values in game code:

.. code-block:: cpp

   #include "Policies/RLToolsNPCPolicy.h"

   // Keep Policy in a UPROPERTY on the calling actor/component.
   Policy = NewObject<URLToolsNPCPolicy>(this);
   Policy->Algorithm = ERLToolsAlgorithm::TD3;
   Policy->Configuration = TEXT("td3/3x2-mlp32");
   Policy->Settings.PublishEveryUpdates = 1;
   Policy->Init(Definition); // Query readiness on later ticks.

   // Once Ready:
   int64 Ticket = INDEX_NONE;
   if (Policy->RequestDecision(Observation, Ticket))
   {
       FAdaptiveDecision Decision;
       if (Policy->TryGetDecision(Ticket, Decision) == EAdaptiveRequestStatus::Ready)
       {
           // Apply Decision.Action in gameplay. Later, after measuring its outcome:
           FAdaptiveExperience Experience;
           Experience.SessionId = Decision.SessionId;
           Experience.AgentId = Decision.AgentId;
           Experience.EpisodeId = Decision.EpisodeId;
           Experience.DecisionId = Decision.DecisionId;
           Experience.PolicyVersion = Decision.PolicyVersion;
           Experience.Observation = Observation;
           Experience.ExecutedAction = Decision.Action;
           Experience.Reward = Reward;
           Experience.NextObservation = NextObservation;
           Experience.bTerminated = bTaskEnded;
           Experience.bTruncated = bTimeLimit;
           Policy->SubmitExperience(Experience);
       }
   }

The Blueprint workflow uses the same reflected nodes and structs. Rewards and
episode conditions remain game-defined; no live-agent coordinator or automatic
environment collection is installed by selecting this policy.

Native actions are normalized to [-1, 1]. The adapter maps them to each declared
Schola bound and preserves the declared shape. SAC/TD3 permit a changed executed
command and unscale it consistently. PPO rejects an additional gameplay action
modification: the native decision retains the original sampled action, likelihood
and value. If gameplay executes the returned command unchanged, the adapter
reuses its original normalized bits instead of introducing scaling-round-trip
error. Public decision/experience fields alone cannot fabricate native provenance.

Unknown decisions, altered attribution/version/originating observations, stale
generations and duplicates of an accepted experience are rejected. Common errors
are available through ``GetLastError`` and ``GetDecisionError``.

Retention and admission
-----------------------

The common API has one retained action-selection ticket. The native adapter also
retains a bounded ledger of retrieved decisions awaiting outcomes:

* ``DecisionCapacity`` defaults to 256 per binding; the oldest ID is evicted when
  a new decision would exceed it.
* ``ExperienceRetentionSeconds`` defaults to 60 seconds. The reward window starts
  at first successful retrieval and is not extended by repeated polling.
* Undelivered decisions are retired when their selection ticket expires or is
  replaced. A retrieved command can receive its reward after the selection
  timeout, within the separate reward-retention window.
* Explicit cancellation, episode reset, learning reset, reinitialization and
  shutdown retire the corresponding decisions. CancelDecision can also retire
  an older retrieved command after a newer ticket has replaced its display slot.
* Accepted submissions consume the native decision. Dropped (full queue), Frozen
  (learning paused), and temporarily NotReady submissions keep it retryable within
  retention limits. PPO may nevertheless reject it after on-policy publication.

Accepted means learner admission, not that an optimizer update has completed.
Statistics expose native admission counters, queue/replay/rollout occupancy,
stored records, actor/critic/target updates, pending work, generation, published
version, learning/training state and last burst duration. Adapter/common validation
rejections occur before native admission and are not counted as native rejections.

Independent and shared ownership
---------------------------------

None is the default shared-group name: identical settings on two NPCs still
create independent learners. A named group explicitly shares one learner in the
same world. Joining requires identical algorithm, profile, AgentType, full
observation/action definitions (bounds and shape), preprocessing and every
learner-wide setting. Incompatible joins fail with a diagnostic; first arrival
does not silently select settings for later incompatible callers.

Bindings have separate agent/episode identities. Inference mode and retention
are binding-local. Episode reset/cancellation affect only that NPC. Pause/resume
and learning reset affect the entire group. Releasing one binding preserves the
learner until its last binding is released. Worlds and PIE instances are isolated.
This is multi-agent contribution to one learner, not shared-trunk/multiple-head
inference.

Controls, operations and snapshots
-----------------------------------

* ``SetLearningEnabled`` closes/resumes admission independently of inference.
  Its operation completes after an active numerical burst has finished. Frozen
  is a distinct admission result.
* ``SetInferenceMode`` selects deterministic or stochastic inference for one
  binding; it does not pause/resume learning.
* ``ResetEpisode`` advances one NPC's episode and retires its decisions while
  preserving weights/replay.
* ``ResetLearning`` reconstructs on a worker. Every shared binding leaves Ready,
  retires outstanding decisions and receives a new session GUID on completion.
  Native generation advances even though policy version restarts at zero.
  The new seed is learner-wide and must match a later group's joining settings.
  Reset returns to enabled learning with empty storage.
* ``Shutdown`` invalidates the binding promptly. Last-owner reclamation and
  synchronization with training take place on a worker.

Supported operations carry IDs. Poll ``GetOperationResult`` for Pending,
Succeeded, Failed or Cancelled; the last 32 results are retained and unknown/retired
IDs report Failed. Shutdown cancels that binding's pending operations. A group
permits one pending learning-control/reset operation at a time; await completion
before requesting another. Initialization has its own observable operation.

``CaptureSnapshot`` returns ``URLToolsPolicySnapshot`` through the common
snapshot interface. Retain it in a reflected reference. It owns an immutable actor,
fixed preprocessing, copied space definitions and action conversion. Evaluate is
deterministic, does not advance exploration or submit experiences, and remains
usable after later training, reset, binding destruction or world teardown.
Captured inference state is not a resumable training checkpoint.

Background execution
--------------------

A world service owns all learners. It submits bounded pool tasks, using a FIFO
ready queue and requeuing a learner after each burst so others can make progress.
At most two tasks run per world by default, configurable from 1 to 8:

.. code-block:: ini

   [/Script/ScholaRLTools.RLToolsWorldSubsystem]
   MaximumConcurrentWorkers=2

Construction, reset, training and destruction of one learner are serialized.
Experience arrival wakes training; Train's remaining-work result reschedules it.
Idle learners are not polled and there is no dedicated thread per NPC. Workers
operate on native owned data, and status delivery checks weak object lifetime and
the original binding ID on the game thread.

The task limits are cooperative work counts, not hard wall-clock deadlines.
An individual numerical update is not preemptible. Pause, stop and teardown close
admission promptly, then let active worker work finish before reclamation.

Validation and limitations
--------------------------

The repository includes ``Examples/NativeLearning``, a minimal host with the
complete plugin and no dependency on example-owned SAC learning code:

.. code-block:: powershell

   ./Examples/NativeLearning/Test-NativeLearning.ps1 -EnginePath 'D:\Apps\Epic Games\UE_5.8'

Automation exercises the actual policy/world service for all three algorithms
and both compiled shapes. It includes non-default bounds, PPO unchanged commands,
attribution, publication, independent/shared ownership, controls, reset, immutable
snapshots, retries, bounded/fair scheduling and world teardown with work in flight.
The host script checks successful results against source registrations.

Supported functionality is CPU, feedforward SAC/TD3/PPO, Box observations and
bounded continuous Box actions with registered configurations. Discrete/Dict/custom
spaces, arbitrary profile generation, recurrent networks, running normalization,
accelerated devices, checkpoints, final separate-project packaging and automatic
game/environment collection remain follow-up capabilities. Rendering and useful
gameplay adaptation require their own validation. The numerical behavior and
configuration extension points are detailed in :doc:`native_rltools_learning`.
