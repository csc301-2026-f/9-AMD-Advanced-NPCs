Adaptive Decision Tickets
=========================

An adaptive NPC asks for an action and keeps the game running while the answer
is prepared. This implementation manages that wait: it issues a ticket,
tracks the answer, and prevents obsolete or invalid actions reaching gameplay.
It builds on :doc:`adaptive_policy_setup` and the team loop in
:doc:`adaptive_online_learning_contract`.

What is implemented
-------------------

``UAdaptiveNPCPolicy`` manages one NPC and one pending decision. It does not
move actors, choose fallback actions, calculate rewards, or train a model.
Gameplay owns movement and rewards; a native learner owns training. The
AdaptiveSAC example's ``UOnlineSACPolicy`` subclasses this base with RLtools.
The mock Adaptive selection on ``UNPCInferencePolicy`` and existing steppers
remain separate from this native path.

Base ``Init`` only validates local definitions. The subclass calls protected
``StartSession`` with valid session, agent and episode IDs once its local learner
is ready. Tests use a small native subclass with controlled answers. Blueprint
cannot manufacture readiness or inject completions.

The gameplay loop
-----------------

Once the session is Ready, the gameplay coordinator can:

1. Call ``RequestDecision(Observation, OutDecisionId, TimeoutSeconds)``. A true
   return supplies a ticket without waiting for training. A subclass may complete
   it synchronously from a snapshot, as ``UOnlineSACPolicy`` does. A false return
   leaves the output ID unchanged; read ``GetLastError``.
2. Call ``TryGetDecision(DecisionId, OutDecision)`` immediately or on later
   updates while playing. This poll does not wait. Only Ready writes the output.
3. Apply a Ready action once, retaining its identity and policy version for
   later experience. Repeated polls return independent copies of the same
   result; gameplay must track whether it has already executed it.
4. Request a new decision when needed. A pending ticket must first finish,
   expire, or be cancelled. A validated dispatch attempt replaces the previous
   terminal/ready slot; older tickets then report Unknown. There is no growing
   history or hidden queue of decisions.

.. list-table:: Poll results
   :header-rows: 1
   :widths: 22 78

   * - Status
     - Meaning
   * - Unknown
     - This ticket is invalid, has never existed, or has been retired.
   * - Pending
     - Waiting for a matching response. No action is available.
   * - Ready
     - A validated action is available and has not passed its deadline.
   * - Failed
     - Dispatch/learner failure or an invalid matching response. Read
       ``GetDecisionError`` for this ticket, or ``GetLastError``.
   * - Cancelled
     - The caller cancelled, the encounter reset, or the policy stopped or
       reinitialized. No action may be used.
   * - Expired
     - The action age limit was reached. Request a fresh decision.

``IsInferenceBusy`` means pending and unexpired, not learner readiness.
Immediate ``Think`` / ``BatchedThink`` remain unavailable and preserve outputs,
even with a Ready session. Existing steppers are not the adaptive coordinator.

Validation and age limits
-------------------------

Requests require a Ready session, a finite positive timeout, and a valid finite
Box observation. Results must match the session, agent, episode and decision
IDs exactly, have a nonnegative policy version, and contain a valid finite Box
action. Schola's existing space validation checks dimensions and bounds;
the bridge additionally checks finite values and explicit shapes. Empty point
shapes are accepted; an explicit shape must match the definition (or the flat
vector length if the definition has no shape). Nothing is silently clamped.

An invalid matching result fails its ticket. Unrelated, duplicate or late
responses cannot replace a result or fail a newer ticket. Lower policy versions
are accepted if otherwise valid; the version identifies which snapshot chose
the action, rather than whether it is the newest available model.

The timeout is an action age limit measured from dispatch on Unreal's local
monotonic clock, including time spent waiting before gameplay reads the answer.
The default is one second for this API, not a team-approved gameplay tuning
value; gameplay should pass a value suitable for its decision cadence. At the
exact deadline both Pending and Ready become Expired. A prompt response cannot be
used indefinitely.

Expiry is evaluated when requesting, polling, cancelling, or processing a
completion. There is no timer or ticking actor. ``IsInferenceBusy`` also checks
the deadline without mutating state; poll before reading a timeout diagnostic.

Reset and stop
--------------

``CancelDecision`` invalidates a retained Pending or Ready ticket. It does not
stop the native learner, reset its weights or remove accepted experience.

``ResetEpisode`` cancels Pending/Ready actions and increments the episode ID.
It retains the session and its readiness, rather than restarting learning.
Gameplay captures the final observation and submits terminal experience before
resetting through the shared ``SubmitExperience`` call. The native subclass
admits validated records to its learner queue through ``DispatchExperience``.

A successful base ``Init`` invalidates old actions and requires a new local
session and calls ``ReleaseLearnerBinding`` for the old Ready binding. An invalid
replacement preserves the previously accepted setup and in-flight work.
``Shutdown`` is idempotent, invalidates the session, releases its native binding,
and prevents more requests until ``Init`` and a new session. Destruction uses the
same release path. ``OnEpisodeReset`` notifies the backend after advancing an
episode without releasing learned state. See :doc:`native_online_learning_foundation`.
Decision IDs are never reused within the policy object's lifetime. Loaded or
duplicated configuration does not inherit a live session or its tickets.

Native integration boundary
---------------------------

``FAdaptiveDecisionRequest`` contains copied observation data plus the four
identity fields. It is a local Unreal record. The protected ``DispatchDecision``
hook must return promptly and copy its input before retaining it. Its base
implementation rejects dispatch. Tests override this hook and the local clock.
``UOnlineSACPolicy`` completes from an immutable
actor snapshot during dispatch.

If a native subclass defers a decision, it must return copied results/failures
to the game thread before invoking ``CompleteDecision`` / ``FailDecision``.
Capture a weak policy pointer and the original request identity, never an actor
or a mutable reference into the policy. If the weak pointer is no longer valid,
discard the callback. Valid pointers are still subject to session/ticket checks, including
after shutdown. A delayed failure must carry the original identity too.
All policy API calls belong on the game thread. No worker thread may directly
mutate this UObject, and no gameplay decision call may wait for training.

Verification
------------

Run the editor automation filter ``Schola.Inference.Adaptive`` for local setup
and lifecycle tests, or ``Schola.Inference`` to include existing paradigm tests.
Controlled responses cover immediate and delayed success, failure, expiry,
invalid data, wrong identities, duplicate callbacks, reset, reinitialization,
shutdown, destruction, copied records and reflected Blueprint calls. The clock
is advanced deterministically without sleeps. These tests demonstrate request
management; the SAC adapter's tests in AdaptiveSAC cover native learner integration.
