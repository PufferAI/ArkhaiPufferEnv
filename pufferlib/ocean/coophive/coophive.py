'''CoopHive seller-side cloud market environment'''

import gymnasium
import numpy as np

import pufferlib
from pufferlib.ocean.coophive import binding

class CoopHive(pufferlib.PufferEnv):
    def __init__(self, num_envs=1, render_mode=None, log_interval=128, buf=None, seed=0,
            episode_length=1000, max_job_duration=100,
            energy_gen=10, energy_storage=100, max_nodes=100, max_space_tb=100,
            buy_price_randomization=0.2, job_efficiency_randomization=0.2,
            reward_scale=0.0001, space_tb_price=0.03,
            a100_node_price=5.31, a100_node_energy_kw=6.5,
            h100_node_price=15.92, h100_node_energy_kw=10.0):
        self.single_observation_space = gymnasium.spaces.Box(low=0, high=1,
            shape=(14,), dtype=np.float32)
        self.single_action_space = gymnasium.spaces.MultiDiscrete([9, 2])
        self.render_mode = render_mode
        self.num_agents = num_envs

        super().__init__(buf)
        self.c_envs = binding.vec_init(self.observations, self.actions, self.rewards,
            self.terminals, self.truncations, num_envs, seed,
            episode_length=episode_length,
            max_job_duration=max_job_duration,
            energy_gen=energy_gen,
            energy_storage=energy_storage,
            max_nodes=max_nodes,
            max_space_tb=max_space_tb,
            buy_price_randomization=buy_price_randomization,
            job_efficiency_randomization=job_efficiency_randomization,
            reward_scale=reward_scale,
            space_tb_price=space_tb_price,
            a100_node_price=a100_node_price,
            a100_node_energy_kw=a100_node_energy_kw,
            h100_node_price=h100_node_price,
            h100_node_energy_kw=h100_node_energy_kw
        )
 
    def reset(self, seed=0):
        binding.vec_reset(self.c_envs, seed)
        return self.observations, []

    def step(self, actions):
        self.actions[:] = actions
        binding.vec_step(self.c_envs)
        info = [binding.vec_log(self.c_envs)]
        return (self.observations, self.rewards,
            self.terminals, self.truncations, info)

    def render(self):
        binding.vec_render(self.c_envs, 0)

    def close(self):
        binding.vec_close(self.c_envs)

if __name__ == '__main__':
    N = 4096
    env = CoopHive(num_envs=N)
    env.reset()
    steps = 0

    CACHE = 1024
    actions = np.random.randn(CACHE, N, env.single_action_space.shape[0])

    import time
    start = time.time()
    while time.time() - start < 10:
        env.step(actions[steps % CACHE])
        steps += 1

    print('Squared SPS:', int(env.num_agents*steps / (time.time() - start)))
