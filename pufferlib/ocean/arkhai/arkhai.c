#include "arkhai.h"

int main() {
    Arkhai env = {
        .tick=0,
        .episode_length=1000,
        .max_job_duration=100,
        .request_timeout=5,
        .buy_price_randomization=0.2,
        .sell_price_randomization=0.2,
        .job_efficiency_randomization=0.2,
        .reward_scale=0.0001,
        .space_tb_price=0.03,
        .a100_node_price=5.31,
        .a100_node_energy_kw=6.5,
        .h100_node_price=15.92,
        .h100_node_energy_kw=10.0,
        .energy_demand_base=1500.0,
        .energy_price_base=20.0,
        .energy_price_sensitivity=0.0001,
        .energy_demand_threshold=1400,
        .a1=-374,
        .b1=-387,
        .a2=-4.6,
        .b2=-17.1,
        .a3=3.2,
        .b3=18.9,
        .randomize_offset=1,
        .preset=NONE,
        .num_agent_sellers=1,
        .num_agent_buyers=1,
    };
    ClusterSpec seller_spec = {
        .node_capacity = 100,
        .node_capacity_dr = 0.2,
        .space_tb = 10000,
        .space_tb_dr = 0.2,
        .energy_gen = 100,
        .energy_gen_dr = 0.2,
        .energy_storage = 1000,
        .energy_storage_dr = 0.2,
        .buy_price = 0.0,
        .buy_price_dr = 0.0,
        .sell_price = 0.0,
        .sell_price_dr = 0.0,
        .job_efficiency = 0.0,
        .job_efficiency_dr = 0.0,
    };
    ClusterSpec buyer_spec = {0};

    //Arkhai env = create_energy_producer();
    //Arkhai env = create_storage_center();
    //Arkhai env = create_premium_hpc();
    int num_agents = env.num_agent_buyers + env.num_agent_sellers;
    init(&env, buyer_spec, seller_spec);
    env.observations = (float*)calloc(num_agents*NUM_OBS, sizeof(float));
    env.actions = (int*)calloc(num_agents*NUM_ACT, sizeof(int));
    env.rewards = (float*)calloc(num_agents, sizeof(float));
    env.terminals = (unsigned char*)calloc(num_agents, sizeof(unsigned char));

    c_reset(&env);
    for (int i=0; i<10000000; i++) {
        env.actions[0] = 1;
        env.actions[1] = 1;
        c_step(&env);
        if (env.terminals[0]) {
            c_reset(&env);
        }
    }
    float n = env.log.n;
    printf("N: %f\n", n);
    printf("Profit: %f\n", env.log.profit/n);
    printf("Buyer Spend: %f\n", env.log.buyer_spend/n);
    printf("Buyer Savings: %f\n", env.log.buyer_savings/n);
    printf("Job Revenue: %f\n", env.log.job_revenue/n);
    printf("Energy Revenue: %f\n", env.log.energy_revenue/n);
    printf("Energy Expense: %f\n", env.log.energy_expense/n);
    printf("Episode Length: %f\n", env.log.episode_length/n);
    printf("Episode Return: %f\n", env.log.episode_return/n);
 
    free(env.observations);
    free(env.actions);
    free(env.rewards);
    free(env.terminals);
    c_close(&env);
}
