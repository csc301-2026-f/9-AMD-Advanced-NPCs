"""
Throwaway experiment: can we drive SB3 SAC by hand (no ``learn()``)?

On Pendulum-v1 this script:
  1. builds an SAC model,
  2. copies the actor weights,
  3. inserts 32 hand-made transitions straight into the replay buffer, with
     actions already scaled to [-1, 1] (the same contract Person C's samples use),
  4. calls ``model.train()`` once,
  5. reports whether the actor weights changed.

Run with:  python Test/adaptive/sac_experiment.py
Not a pytest module (no ``test_`` prefix), so it is not collected.
"""

import copy

import gymnasium as gym
import numpy as np
import stable_baselines3
import torch as th
from stable_baselines3 import SAC
from stable_baselines3.common.logger import configure

N_SAMPLES = 32
SEED = 0


def max_param_diff(before: dict[str, th.Tensor], module: th.nn.Module) -> float:
    after = module.state_dict()
    return max((after[k] - before[k]).abs().max().item() for k in before)


def main() -> None:
    print(f"stable-baselines3 {stable_baselines3.__version__}, torch {th.__version__}")

    env = gym.make("Pendulum-v1")
    model = SAC(
        "MlpPolicy",
        env,
        learning_starts=0,
        batch_size=N_SAMPLES,
        buffer_size=1_000,
        seed=SEED,
        device="cpu",
    )
    print(f"env action space: {env.action_space}  (SB3 stores actions rescaled to [-1, 1])")
    print(f"replay buffer class: {type(model.replay_buffer).__name__}, "
          f"handle_timeout_termination={model.replay_buffer.handle_timeout_termination}")

    # 1) Copy actor (and critic, for comparison) weights before training.
    actor_before = copy.deepcopy(model.actor.state_dict())
    critic_before = copy.deepcopy(model.critic.state_dict())

    probe_obs = np.array([[1.0, 0.0, 0.0]], dtype=np.float32)
    probe_action_before, _ = model.predict(probe_obs, deterministic=True)

    # 2) Hand-make 32 transitions. Actions are sampled directly in [-1, 1];
    #    only the env.step call sees the unscaled [-2, 2] torque.
    rng = np.random.default_rng(SEED)
    obs, _ = env.reset(seed=SEED)
    for _ in range(N_SAMPLES):
        scaled_action = rng.uniform(-1.0, 1.0, size=(1,)).astype(np.float32)
        env_action = model.policy.unscale_action(scaled_action)
        next_obs, reward, terminated, truncated, _ = env.step(env_action)
        model.replay_buffer.add(
            obs[None],
            next_obs[None],
            scaled_action[None],
            np.array([reward], dtype=np.float32),
            np.array([terminated or truncated]),
            [{"TimeLimit.truncated": truncated and not terminated}],
        )
        obs = next_obs if not (terminated or truncated) else env.reset()[0]

    rb = model.replay_buffer
    print(f"buffer size after insert: {rb.size()}  "
          f"stored action range: [{rb.actions[:rb.size()].min():.3f}, {rb.actions[:rb.size()].max():.3f}]")

    # 3) train() without learn(): the logger is only created inside learn().
    try:
        model.train(gradient_steps=1, batch_size=N_SAMPLES)
        print("train() without a logger: worked (unexpected)")
    except AttributeError as e:
        print(f"train() without a logger: AttributeError -> {e}")
    model.set_logger(configure(folder=None, format_strings=[]))

    n_updates_before = model._n_updates
    model.train(gradient_steps=1, batch_size=N_SAMPLES)

    # 4) Did anything change?
    actor_diff = max_param_diff(actor_before, model.actor)
    critic_diff = max_param_diff(critic_before, model.critic)
    probe_action_after, _ = model.predict(probe_obs, deterministic=True)

    print(f"_n_updates: {n_updates_before} -> {model._n_updates}")
    print(f"actor  max |dW| = {actor_diff:.3e}  -> changed: {actor_diff > 0}")
    print(f"critic max |dW| = {critic_diff:.3e}  -> changed: {critic_diff > 0}")
    print(f"deterministic action on probe obs: {probe_action_before.ravel()} -> {probe_action_after.ravel()}")

    env.close()


if __name__ == "__main__":
    main()
