Native RLtools checkpoints
==========================

A native RLtools learner keeps its networks, optimizers and replay storage in
memory. Without a checkpoint, that state is lost when the learner is reset,
shut down or the game exits. Checkpoints write that state to a file and later
restore it into a running learner.

Persistence is opt-in. Nothing is saved automatically. Exporting a policy,
saving training state and including replay contents are separate choices.
A native checkpoint is not an ONNX model and cannot be loaded by
``UNNEPolicy``; it is read only by the same compiled ``ScholaRLTools``
configuration that wrote it.

Checkpoint modes
----------------

The mode is chosen at save time and recorded in the file. Load restores exactly
what the file contains and nothing more.

.. list-table:: Checkpoint contents by mode
   :header-rows: 1
   :widths: 40 20 20 20

   * - State
     - ``PolicyExport``
     - ``TrainingState``
     - ``FullResume``
   * - Actor (and PPO value function)
     - Yes
     - Yes
     - Yes
   * - Critics and target networks
     - No
     - Yes
     - Yes
   * - SAC entropy temperature (part of the actor)
     - Yes
     - Yes
     - Yes
   * - Optimizer state
     - No
     - Yes
     - Yes
   * - Training RNG
     - No
     - Yes
     - Yes
   * - Policy version
     - Yes
     - Yes
     - Yes
   * - Update counters
     - No
     - Yes
     - Yes
   * - Off-policy replay buffer contents
     - No
     - No
     - Yes (SAC/TD3)
   * - Fixed observation preprocessing
     - Fingerprint only
     - Fingerprint only
     - Fingerprint only

* ``PolicyExport`` is for starting from pretrained weights. After load, critics
  and optimizer state are freshly initialized and the replay is empty.
* ``TrainingState`` is the default. It keeps everything needed to continue
  optimizing, but the replay buffer is not saved. After load the learner refills
  it and warms up again (``LearningStarts`` applies). This is not a full resume.
* ``FullResume`` adds the replay buffer so SAC/TD3 training continues where it
  stopped. Files are larger, roughly proportional to the compiled replay capacity.

PPO has no persistent replay. Its pending rollout is never saved and is
discarded on load, so ``FullResume`` is equivalent to ``TrainingState`` for PPO;
such a file records a replay size of 0.

Preprocessing values are not stored. The file records a fingerprint of the
learner's ``ObservationMean``/``ObservationStd``, and load requires the same
values to be configured already.

After any successful load, SAC/TD3 skip the uniform random warm-up actions
(``InitialRandomSteps``): the restored actor chooses actions from the first
decision.

Saving and loading
------------------

``URLToolsNPCPolicy`` exposes two operations, usable from Blueprint and C++:

* ``SaveCheckpoint(Path, Mode)``, with ``Mode`` defaulting to ``TrainingState``.
* ``LoadCheckpoint(Path)``. The mode is read from the file.

Both are asynchronous and return an ``FAdaptiveOperationResult``. The returned
status is Pending while a worker does the file and numerical work. Poll
``GetOperationResult`` with its ``OperationId`` on later ticks until it reports
Succeeded or Failed; ``Diagnostic`` describes a failure. Only one checkpoint or learning-control operation can be pending per learner at
a time; a second request fails immediately. Do not busy-wait on the gameplay thread.

In Blueprint, call **Save Checkpoint** or **Load Checkpoint** on the policy
reference, store the returned operation ID, and check **Get Operation Result**
from a timer or tick until its status is no longer Pending.

.. code-block:: cpp

   // Policy is a Ready URLToolsNPCPolicy held in a UPROPERTY.
   FAdaptiveOperationResult Save =
       Policy->SaveCheckpoint(TEXT("Guard/latest.ckpt"), ERLToolsCheckpointMode::FullResume);
   SaveOperation = Save.OperationId;

   // On a later tick:
   const FAdaptiveOperationResult Result = Policy->GetOperationResult(SaveOperation);
   if (Result.Status == EAdaptiveOperationStatus::Failed)
   {
       UE_LOG(LogTemp, Warning, TEXT("Checkpoint save failed: %s"), *Result.Diagnostic);
   }

   // Later, possibly in a new session, after Init has completed:
   LoadOperation = Policy->LoadCheckpoint(TEXT("Guard/latest.ckpt")).OperationId;

Saving does not interrupt inference. It waits for an active training burst to
finish, then writes ``Path.tmp`` and renames it over ``Path``. A failed save
never leaves a partial file at the destination.

File locations
--------------

An empty or relative path is resolved under ``<Project>/Saved/Schola/Learning``.
Absolute paths are used as given. An empty path, or one ending in a separator,
gets a file name from the learner's configuration id, for example
``sac_3x2-mlp32.ckpt``. There is no rotation: saving to the same path replaces the
file.

A checkpoint contains plain numerical state. It does not persist UObject
pointers, actors, sessions, decisions or bindings. The learner it is loaded
into must already exist and be Ready.

What load validates
-------------------

Load validates the file before it changes anything. The header is read first,
then the profile, RLtools revision and preprocessing are checked, and the
declared payload size must equal what this build would write before anything is
allocated. Then the checksum, which covers the header and the payload, and the
payload structure are checked. Only then is the
payload deserialized into a new replacement state.

Each file records its format version, RLtools revision, algorithm and profile
identifier, observation/action dimensions, preprocessing fingerprint, mode,
policy version, update count, replay size and a checksum.

.. list-table:: Load failures
   :header-rows: 1
   :widths: 20 80

   * - Result
     - Meaning
   * - ``IoError``
     - The file is missing or unreadable (or, for save, unwritable).
   * - ``BadFormat``
     - The file is not a Schola RLtools checkpoint, is truncated or corrupt,
       fails its checksum, or has an unknown format version.
   * - ``Mismatch``
     - The file is valid but was written for a different profile, algorithm,
       dimensions, RLtools revision, preprocessing or network structure.
   * - ``Unsupported``
     - The request is valid but this build cannot satisfy it.
   * - ``NotReady``
     - The learner is stopped.

A failed load leaves the live learner unchanged. While any load is in flight,
the learner's bindings are paused: decisions fail and submissions return
``NotReady``. After a failure the learner returns to Ready with the same session,
and its outstanding decisions remain valid.

Effects of a successful load
----------------------------

Load acts on the whole logical learner. For a named ``SharedGroup`` every member
binding is affected, not only the caller. On success the replacement state is
swapped in the same way as ``ResetLearning``:

* Native generation advances and each binding receives a new session GUID.
* Queued records and all outstanding decisions are retired. Experiences for
  decisions made before the load are rejected.
* The published policy version becomes the larger of the current version plus
  one and the saved version. Version never goes backwards, so a gameplay check
  for "newer policy" stays valid across loads.

The file's fixed preprocessing must match the learner's ``ObservationMean`` /
``ObservationStd``; otherwise load fails with ``Mismatch``.

Compatibility
-------------

A checkpoint is tied to the compiled configuration and the pinned RLtools
revision that wrote it. Regenerating a project configuration (see
:doc:`rltools_configuration_generation`) or upgrading RLtools makes older files
fail with ``Mismatch`` even if the dimensions are unchanged. Keep the
configuration asset and plugin version alongside any checkpoints you intend to
reuse. There is no migration between formats or profiles.

Limitations and verification
----------------------------

* The header is written little-endian. Files have not been verified to load
  across platforms or compilers; treat them as specific to the platform that
  wrote them.
* Only CPU feedforward SAC, TD3 and PPO profiles are supported, matching
  :doc:`native_rltools_learning`.
* ``Schola.RLTools`` Unreal automation tests cover save/load. They were authored
  on macOS and must be run on the Windows UE 5.8 target before relying on them.
* The native learner tests, including checkpoint round trips and corrupted or
  mismatched files, run without Unreal through
  ``Tools/ScholaRLToolsNative/build.sh``.

