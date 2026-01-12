#include "arkhai.h"

Arkhai create_test_env() {
    return (Arkhai) {
        .tick=0,
        .episode_length=96,
        .job_duration=10,
        .job_duration_dr=0.0,
        .request_timeout=5,
        .scripted_buy_price=1.0,
        .scripted_sell_price=1.0,
        .scripted_buy_price_dr=0.0,
        .scripted_sell_price_dr=0.0,
        .job_efficiency=1.0,
        .job_efficiency_dr=0.0,
        .job_tb_usage=0,
        .job_nodes=1,
        .reward_scale=0.0001,
        .tb_price=0.03,
        .a100_price=5,
        .a100_kw=6.5,
        .h100_price=5,
        .h100_kw=10.0,
        .energy_demand_base=0.0,
        .kwh_price_base=0.0,
        .kwh_price_sensitivity=0.0,
        .kwh_demand_threshold=0,
        .a1=0,
        .b1=0,
        .a2=0,
        .b2=0,
        .a3=0,
        .b3=0,
        .randomize_offset=0,
        .preset=NONE,
        .ai_sellers=0,
        .ai_buyers=0,
        .scripted_sellers=1,
        .scripted_buyers=1,
        .debug=true,
    };
}

ClusterSpec create_test_spec() {
    return (ClusterSpec) {
        .node_capacity = 1,
        .node_capacity_dr = 0.0,
        .tb_capacity = 10000,
        .tb_capacity_dr = 0.0,
        .kwh_capacity = 1000,
        .kwh_capacity_dr = 0.0,
        .kw_generation = 100,
        .kw_generation_dr = 0.0,
    };
}
 
int main() {
    // Basic sanity: fill 10 jobs. Note: revenue gets recognized upfront,
    // so the expected output is 1000 instead of 960
    printf("Basic sanity check\n");
    Arkhai env = create_test_env();
    ClusterSpec seller_spec = create_test_spec();
    ClusterSpec buyer_spec = {0};
    int num_agents = env.ai_buyers + env.ai_sellers;
    init(&env, buyer_spec, seller_spec);
    env.observations = (float*)calloc(num_agents*NUM_OBS, sizeof(float));
    env.actions = (int*)calloc(num_agents*NUM_ACT, sizeof(int));
    env.rewards = (float*)calloc(num_agents, sizeof(float));
    env.terminals = (unsigned char*)calloc(num_agents, sizeof(unsigned char));
    c_reset(&env);
    for (int i=0; i<env.episode_length; i++){
        c_step(&env);
    }
    assert(env.agents[0].job_revenue == 1000.0f && "Failed basic sale check");
    c_step(&env);
    free(env.observations);
    free(env.actions);
    free(env.rewards);
    free(env.terminals);
    c_close(&env);
    printf("Passed basic sanity check\n\n");

    // Energy production with no storage
    printf("Energy production with no storage\n");
    env = create_test_env();
    env.kwh_price_base = 1.0f;
    seller_spec = create_test_spec();
    seller_spec.kw_generation = 1,
    seller_spec.node_capacity = 0;
    seller_spec.kwh_capacity = 0;
    buyer_spec = (ClusterSpec){0};
    num_agents = env.ai_buyers + env.ai_sellers;
    init(&env, buyer_spec, seller_spec);
    env.observations = (float*)calloc(num_agents*NUM_OBS, sizeof(float));
    env.actions = (int*)calloc(num_agents*NUM_ACT, sizeof(int));
    env.rewards = (float*)calloc(num_agents, sizeof(float));
    env.terminals = (unsigned char*)calloc(num_agents, sizeof(unsigned char));
    c_reset(&env);
    for (int i=0; i<env.episode_length; i++){
        c_step(&env);
    }
    assert(env.agents[0].energy_revenue == env.episode_length && "Failed energy production check");
    c_step(&env);
    free(env.observations);
    free(env.actions);
    free(env.rewards);
    free(env.terminals);
    c_close(&env);
    printf("Passed energy production with no storage\n\n");

    // Bilateral agent negotiation
    printf("Bilateral agent negotiation\n");
    env = create_test_env();
    env.ai_sellers = 1;
    env.ai_buyers = 1;
    env.scripted_sellers = 0;
    env.scripted_buyers = 0;
    seller_spec = create_test_spec();
    buyer_spec = (ClusterSpec){0};
    num_agents = env.ai_buyers + env.ai_sellers;
    init(&env, buyer_spec, seller_spec);
    env.observations = (float*)calloc(num_agents*NUM_OBS, sizeof(float));
    env.actions = (int*)calloc(num_agents*NUM_ACT, sizeof(int));
    env.rewards = (float*)calloc(num_agents, sizeof(float));
    env.terminals = (unsigned char*)calloc(num_agents, sizeof(unsigned char));
    c_reset(&env);
    for (int i=0; i<2*env.episode_length; i++){
        if (i%2 == 0) {
            env.actions[0] = 4; // Seller offers midpoint
            env.actions[2] = 2; // Buyer offers very low
        } else {
            env.actions[0] = 3; // Seller offers discount
            env.actions[2] = 3; // Buyer matches
        }
        c_step(&env);
    }
    assert(env.agents[0].job_revenue == 950.0f && "Bilateral negotiation seller incorrect revenue");
    assert(env.agents[1].job_revenue == 1000.0f && "Bilateral negotiation buyer incorrect revenue");
    assert(env.agents[1].job_expense == 950.0f && "Bilateral negotiation buyer incorrect expense");
    c_step(&env);
    free(env.observations);
    free(env.actions);
    free(env.rewards);
    free(env.terminals);
    c_close(&env);
    printf("Passed bilateral agent negotiation\n");
}
