#include "arkhai.h"

int main() {
    Arkhai env = {
        .tick=0,
        .episode_length=1000,
        .job_duration=100,
        .job_duration_dr=0.2,
        .request_timeout=5,
        .scripted_buy_price_dr=0.2,
        .scripted_sell_price_dr=0.2,
        .job_efficiency=0.8,
        .job_efficiency_dr=0.2,
        .reward_scale=0.0001,
        .tb_price=0.03,
        .a100_price=5.31,
        .a100_kw=6.5,
        .h100_price=15.92,
        .h100_kw=10.0,
        .energy_demand_base=1500.0,
        .kwh_price_base=20.0,
        .kwh_price_sensitivity=0.0001,
        .kwh_demand_threshold=1400,
        .a1=-374,
        .b1=-387,
        .a2=-4.6,
        .b2=-17.1,
        .a3=3.2,
        .b3=18.9,
        .randomize_offset=1,
        .preset=NONE,
        .ai_sellers=1,
        .ai_buyers=1,
        .scripted_sellers=0,
        .scripted_buyers=0,
    };
    ClusterSpec seller_spec = {
        .node_capacity = 100,
        .node_capacity_dr = 0.2,
        .tb_capacity = 10000,
        .tb_capacity_dr = 0.2,
        .kwh_capacity = 1000,
        .kwh_capacity_dr = 0.2,
        .kw_generation = 100,
        .kw_generation_dr = 0.2,
    };
    ClusterSpec buyer_spec = {0};

    //Arkhai env = create_energy_producer();
    //Arkhai env = create_storage_center();
    //Arkhai env = create_premium_hpc();
    int num_agents = env.ai_buyers + env.ai_sellers;
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
