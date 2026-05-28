#include <assert.h>
#include "arkhai.h"


void allocate_buffers(Arkhai* env, int num_agents) {
    env->observations = (float*)calloc(num_agents*NUM_OBS, sizeof(float));
    env->actions = (int*)calloc(num_agents*NUM_ACT, sizeof(int));
    env->rewards = (float*)calloc(num_agents, sizeof(float));
    env->terminals = (unsigned char*)calloc(num_agents, sizeof(unsigned char));
}

void free_buffers(Arkhai* env) {
    free(env->observations);
    free(env->actions);
    free(env->rewards);
    free(env->terminals);
}

bool is_close(float a, float b) {
    return fabsf(a-b) < 1e-6;
}

// Annoying: we have to dupe all the params from base. I can add a simple C ini
// parser in the next version. We have this for some tests in 4.0 and it works well.
Arkhai create_train_env() {
    return (Arkhai) {
        .tick=0,
        .ai_sellers=1,
        .ai_buyers=0,
        .scripted_sellers=0,
        .scripted_buyers=1,
        .episode_length=100,
        .request_timeout=5,
        .job_nodes={10, 10, 10},
        .job_nodes_dr={0.2, 0.2, 0.2},
        .job_duration=10,
        .job_duration_dr=0.2,
        .job_tb_usage=0.2,
        .job_tb_usage_dr=0.2,
        .job_efficiency=0.8,
        .job_efficiency_dr=0.2,
        .scripted_buy_price=0.9,
        .scripted_buy_price_dr=0.2,
        .scripted_sell_price=0.9,
        .scripted_sell_price_dr=0.2,
        .reward_scale=0.0001,
        .tb_price=0.03,
        .a100_price=5.31,
        .a100_kw=6.5,
        .h100_price=15.92,
        .h100_kw=10.0,
        .r5090_price=0.37,
        .r5090_kw=1.0,
        .gh200_price=52.0,
        .gh200_kw=10.0,
        .gb200_price=32.0,
        .gb200_kw=13.3,
        .energy_demand_base=1500.0,
        .kwh_price_base=0.02,
        .kwh_price_sensitivity=0.0000001,
        .kwh_demand_threshold=1400,
        .a1=-374,
        .b1=-387,
        .a2=-4.6,
        .b2=-17.1,
        .a3=3.2,
        .b3=18.9,
        .debug=false,
    };
}

ClusterSpec create_train_spec() {
    return (ClusterSpec) {
        .node_capacity = {100, 100, 100},
        .node_capacity_dr = {0.2, 0.2, 0.2},
        .tb_capacity = 100,
        .tb_capacity_dr = 0.2,
        .kwh_capacity = 100,
        .kwh_capacity_dr = 0.2,
        .kw_generation = 10,
        .kw_generation_dr = 0.2,
    };
}

Arkhai create_test_env() {
    return (Arkhai) {
        .tick=0,
        .episode_length=100,
        .job_duration=10,
        .job_duration_dr=0.0,
        .request_timeout=5,
        .scripted_buy_price=1.0,
        .scripted_buy_price_dr=0.0,
        .scripted_sell_price=1.0,
        .scripted_sell_price_dr=0.0,
        .job_efficiency=1.0,
        .job_efficiency_dr=0.0,
        .job_tb_usage=0,
        .job_nodes={1, 1, 1},
        .job_nodes_dr={0.0, 0.0, 0.0},
        .reward_scale=0.0001,
        .tb_price=0.03,
        .a100_price=5,
        .a100_kw=6.5,
        .h100_price=5,
        .h100_kw=10.0,
        .r5090_price=0.0,
        .r5090_kw=1.0,
        .gh200_price=0.0,
        .gh200_kw=10.0,
        .gb200_price=0.0,
        .gb200_kw=13.3,
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
        .node_capacity = {1, 1, 1},
        .node_capacity_dr = {0.0, 0.0, 0.0},
        .tb_capacity = 10000,
        .tb_capacity_dr = 0.0,
        .kwh_capacity = 1000,
        .kwh_capacity_dr = 0.0,
        .kw_generation = 100,
        .kw_generation_dr = 0.0,
    };
}

void test_sanity() {
    // Basic sanity: fill 10 jobs. Note: revenue gets recognized upfront,
    // so the expected output is 1000 instead of 960
    printf("Basic sanity check\n");
    Arkhai env = create_test_env();
    ClusterSpec seller_spec = create_test_spec();
    ClusterSpec buyer_spec = {0};
    int num_agents = env.ai_buyers + env.ai_sellers;
    init(&env, buyer_spec, seller_spec);
    allocate_buffers(&env, num_agents);
    c_reset(&env);
    for (int i=0; i<env.episode_length; i++){
        c_step(&env);
    }
    assert(env.agents[0].job_revenue == 1000.0f && "Failed basic sale check");
    c_step(&env);
    free_buffers(&env);
    c_close(&env);
    printf("Passed basic sanity check\n\n");
}

void test_storage() {
    printf("Basic storage check\n");
    Arkhai env = create_test_env();
    for (int i=0; i<NODE_TYPES; i++) {
        env.job_nodes[i] = 0;
    }
    env.job_tb_usage = 10.0f;
    env.tb_price = 1.0f;
    ClusterSpec seller_spec = create_test_spec();
    seller_spec.tb_capacity = 100;
    for (int i=0; i<NODE_TYPES; i++) {
        seller_spec.node_capacity[i] = 0;
    }
    ClusterSpec buyer_spec = {0};
    int num_agents = env.ai_buyers + env.ai_sellers;
    init(&env, buyer_spec, seller_spec);
    allocate_buffers(&env, num_agents);
    c_reset(&env);
    for (int i=0; i<10; i++){
        c_step(&env);
    }
    assert(env.agents[0].job_revenue == 1000.0f && "Failed basic storage check");
    c_step(&env);
    free_buffers(&env);
    c_close(&env);
    printf("Passed basic storage check\n\n");
}


void test_energy_production() {
    printf("Energy production\n");
    Arkhai env = create_test_env();
    env.scripted_sellers = 0;
    env.ai_sellers = 1;
    env.kwh_price_base = 1.0f;
    ClusterSpec seller_spec = create_test_spec();
    seller_spec.kw_generation = 1;
    memset(seller_spec.node_capacity, 0, sizeof(int)*NODE_TYPES);
    seller_spec.kwh_capacity = 10;
    ClusterSpec buyer_spec = (ClusterSpec){0};
    int num_agents = env.ai_buyers + env.ai_sellers;
    init(&env, buyer_spec, seller_spec);
    allocate_buffers(&env, num_agents);
    c_reset(&env);
    for (int i=0; i<env.episode_length; i++){
        env.actions[1] = 4; // Don't sell
        if (i == 9) {
            env.actions[1] = 1; // Sell half
            c_step(&env);
            assert(env.agents[0].energy_revenue == 5.0f && "Failed manual energy sale check");
        } else if (i >= 20) {
            c_step(&env);
            assert(env.agents[0].energy_revenue == i - 9 && "Failed automatic energy sale check");
        } else {
            c_step(&env);
        }
    }
    c_step(&env);
    free_buffers(&env);
    c_close(&env);
    printf("Passed energy sale\n\n");
}

void test_bilateral_negotiation() {
    // Bilateral agent negotiation
    printf("Bilateral agent negotiation\n");
    Arkhai env = create_test_env();
    env.ai_sellers = 1;
    env.ai_buyers = 1;
    env.scripted_sellers = 0;
    env.scripted_buyers = 0;
    ClusterSpec seller_spec = create_test_spec();
    ClusterSpec buyer_spec = {0};
    int num_agents = env.ai_buyers + env.ai_sellers;
    init(&env, buyer_spec, seller_spec);
    allocate_buffers(&env, num_agents);
    c_reset(&env);
    for (int i=0; i<2*env.episode_length; i++){
        int tick_before = env.tick;
        if (i%2 == 0) {
            env.actions[0] = 5; // Seller offers midpoint
            env.actions[2] = 3; // Buyer offers very low
            c_step(&env);
            // Negotiation can fail due to lack of resources on later ticks
            if (i == 0) {
                assert(env.tick == tick_before && "tick advanced on failed negotiation");
            }
        } else {
            env.actions[0] = 4; // Seller offers discount
            env.actions[2] = 4; // Buyer matches
            c_step(&env);
            assert(env.tick != tick_before && "tick did not advance on completed negotiation");
        }
    }
    assert(env.agents[0].job_revenue == 950.0f && "Bilateral negotiation seller incorrect revenue");
    assert(env.agents[1].job_revenue == 1000.0f && "Bilateral negotiation buyer incorrect revenue");
    assert(env.agents[1].compute_expense == 950.0f && "Bilateral negotiation buyer incorrect expense");
    c_step(&env);
    free_buffers(&env);
    c_close(&env);
    printf("Passed bilateral agent negotiation\n\n");
}

Log run_determinism_trial(unsigned int seed) {
    srand(seed);
    Arkhai env = create_train_env();
    ClusterSpec seller_spec = create_train_spec();
    ClusterSpec buyer_spec = {0};
    int num_agents = env.ai_buyers + env.ai_sellers;
    init(&env, buyer_spec, seller_spec);
    allocate_buffers(&env, num_agents);
    c_reset(&env);
    for (int i=0; i<100000; i++) {
        env.actions[0] = 3;
        c_step(&env);
    }
    Log log = env.log;
    c_step(&env);
    free_buffers(&env);
    c_close(&env);
    return log;
}

void assert_logs_match(Log a, Log b) {
    assert(is_close(a.score, b.score) && "Determinism check failed: score");
    assert(is_close(a.expense, b.expense) && "Determinism check failed: expense");
    assert(is_close(a.profit, b.profit) && "Determinism check failed: profit");
    assert(is_close(a.energy_revenue, b.energy_revenue) && "Determinism check failed: energy_revenue");
    assert(is_close(a.energy_expense, b.energy_expense) && "Determinism check failed: energy_expense");
    assert(is_close(a.episode_length, b.episode_length) && "Determinism check failed: episode_length");
    assert(is_close(a.episode_return, b.episode_return) && "Determinism check failed: episode_return");
    assert(is_close(a.jobs_completed, b.jobs_completed) && "Determinism check failed: jobs_completed");
    assert(is_close(a.capacity_used, b.capacity_used) && "Determinism check failed: capacity_used");
    assert(is_close(a.n, b.n) && "Determinism check failed: n");
}

void test_determinism() {
    // Training environment
    printf("Mirrored training environment\n");
    Log log_a = run_determinism_trial(0);
    Log log_b = run_determinism_trial(0);
    assert_logs_match(log_a, log_b);
    printf("\tProfit: %f\n", log_a.profit / log_a.n);
    printf("\tExpense: %f\n", log_a.expense / log_a.n);
    printf("\tEpisode length: %f\n", log_a.episode_length / log_a.n);
    printf("\tEpisode return: %f\n", log_a.episode_return / log_a.n);
    printf("\tJobs completed: %f\n", log_a.jobs_completed / log_a.n);
    printf("\tCapacity used: %f\n", log_a.capacity_used / log_a.n);
    printf("\tN: %f\n", log_a.n);
    printf("Finished determinism test\n\n");
}

void test_200x5090() {
    // Selling 200x 5090 test
    printf("Selling 200x single 5090s\n");
    Arkhai env = create_train_env();
    env.scripted_buy_price=1.0,
    env.scripted_buy_price_dr=0.0,
    env.job_duration=100;
    env.job_duration_dr=0;
    env.job_nodes[A100] = 0;
    env.job_nodes[H100] = 0;
    env.job_nodes[R5090] = 200;
    env.job_nodes_dr[R5090] = 0.0;
    env.job_tb_usage=1.0;
    env.job_tb_usage_dr=0.0;
 
    ClusterSpec seller_spec = {
        .node_capacity = {0, 0, 200},
        .node_capacity_dr = {0.0, 0.0, 0.0},
        .tb_capacity = 200,
        .tb_capacity_dr = 0.0,
        .kwh_capacity = 0,
        .kwh_capacity_dr = 0.0,
        .kw_generation = 0,
        .kw_generation_dr = 0.0,
    };
    ClusterSpec buyer_spec = {0};
    int num_agents = env.ai_buyers + env.ai_sellers;
    init(&env, buyer_spec, seller_spec);
    allocate_buffers(&env, num_agents);
    c_reset(&env);

    while (env.terminals[0] == 0) {
        env.actions[0] = 5;
        c_step(&env);
    }
    printf("\tProfit: %f\n", env.log.profit / env.log.n);
    printf("\tExpense: %f\n", env.log.expense / env.log.n);
    printf("\tEpisode length: %f\n", env.log.episode_length / env.log.n);
    printf("\tEpisode return: %f\n", env.log.episode_return / env.log.n);
    printf("\tN: %f\n", env.log.n);
    c_step(&env);
    free_buffers(&env);
    c_close(&env);
    printf("Finished 200x single 5090s\n");
}

void test_calcul() {
    // Selling 200x 5090 test
    printf("Calcul test\n");
    Arkhai env = create_train_env();
    // Using a residential energy price to create room to test energy optimization
    env.kwh_price_base = 0.15;
    env.episode_length = 96;
    env.scripted_buy_price=1.0,
    env.scripted_buy_price_dr=0.0,
    env.job_duration=24;
    env.job_duration_dr=0.5;
    env.a100_price = 5.31;
    env.job_nodes[A100] = 16;
    env.job_nodes[H100] = 0;
    env.job_nodes[R5090] = 0;
    env.job_nodes_dr[A100] = 15.0f/16.0f;
    env.job_tb_usage=0.0;
    env.job_tb_usage_dr=0.0;
 
    ClusterSpec seller_spec = {
        .node_capacity = {80, 0, 0},
        .node_capacity_dr = {0.0, 0.0, 0.0},
        .tb_capacity = 0,
        .tb_capacity_dr = 0.0,
        .kwh_capacity = 1000,
        .kwh_capacity_dr = 0.0,
        .kw_generation = 0,
        .kw_generation_dr = 0.0,
    };
    ClusterSpec buyer_spec = {0};
    int num_agents = env.ai_buyers + env.ai_sellers;
    init(&env, buyer_spec, seller_spec);
    allocate_buffers(&env, num_agents);
    c_reset(&env);

    for (int i=0; i<10000; i++) {
        env.actions[0] = 5;

        if (env.observations[0] == 0) {
            env.actions[1] = 8;
        } else if (env.observations[0] == 0.5) {
            env.actions[1] = 3;
        } else {
            env.actions[1] = 4;
        }

        c_step(&env);
    }
    printf("\tCeiling: %f\n", env.episode_length * seller_spec.node_capacity[A100] * env.a100_price);
    printf("\tProfit: %f\n", env.log.profit / env.log.n);
    printf("\tExpense: %f\n", env.log.expense / env.log.n);
    printf("\tEpisode length: %f\n", env.log.episode_length / env.log.n);
    printf("\tEpisode return: %f\n", env.log.episode_return / env.log.n);
    printf("\tJobs completed: %f\n", env.log.jobs_completed / env.log.n);
    printf("\tCapacity used: %f\n", env.log.capacity_used / env.log.n);
    printf("\tN: %f\n", env.log.n);
    c_step(&env);
    free_buffers(&env);
    c_close(&env);
    printf("Finished Calcul test\n");
}

float node_price_for_type(Arkhai* env, int node_type) {
    if (node_type == A100) {
        return env->a100_price;
    } else if (node_type == H100) {
        return env->h100_price;
    } else if (node_type == R5090) {
        return env->r5090_price;
    } else if (node_type == GH200) {
        return env->gh200_price;
    } else if (node_type == GB200) {
        return env->gb200_price;
    }
    assert(false && "Unknown node type");
    return 0.0f;
}

float node_kw_for_type(Arkhai* env, int node_type) {
    if (node_type == A100) {
        return env->a100_kw;
    } else if (node_type == H100) {
        return env->h100_kw;
    } else if (node_type == R5090) {
        return env->r5090_kw;
    } else if (node_type == GH200) {
        return env->gh200_kw;
    } else if (node_type == GB200) {
        return env->gb200_kw;
    }
    assert(false && "Unknown node type");
    return 0.0f;
}

void test_power_site(char* name, int node_type, int gpus, float power_mw) {
    printf("%s\n", name);
    Arkhai env = create_train_env();
    env.kwh_price_base = 0.15;
    env.episode_length = 96;
    env.scripted_buy_price=1.0,
    env.scripted_buy_price_dr=0.0,
    env.job_duration=24;
    env.job_duration_dr=0.5;
    env.job_tb_usage=0.0;
    env.job_tb_usage_dr=0.0;

    for (int i=0; i<NODE_TYPES; i++) {
        env.job_nodes[i] = 0;
        env.job_nodes_dr[i] = 0.0f;
    }

    int pods = gpus / 8;
    int job_pods = pods / 5;
    assert(gpus % 8 == 0 && "Expected GPU count divisible by 8 pods");
    assert(job_pods > 0 && "Expected at least one 8-GPU pod per job");
    env.job_nodes[node_type] = job_pods;
    env.job_nodes_dr[node_type] = (job_pods - 1.0f) / job_pods;

    ClusterSpec seller_spec = {
        .node_capacity = {0, 0, 0, 0, 0},
        .node_capacity_dr = {0.0, 0.0, 0.0, 0.0, 0.0},
        .tb_capacity = 0,
        .tb_capacity_dr = 0.0,
        .kwh_capacity = power_mw * 1000.0f,
        .kwh_capacity_dr = 0.0,
        .kw_generation = 0,
        .kw_generation_dr = 0.0,
    };
    seller_spec.node_capacity[node_type] = pods;

    ClusterSpec buyer_spec = {0};
    int num_agents = env.ai_buyers + env.ai_sellers;
    init(&env, buyer_spec, seller_spec);
    allocate_buffers(&env, num_agents);
    c_reset(&env);

    for (int i=0; i<10000; i++) {
        env.actions[0] = 5;

        if (env.observations[0] == 0) {
            env.actions[1] = 8;
        } else if (env.observations[0] == 0.5) {
            env.actions[1] = 3;
        } else {
            env.actions[1] = 4;
        }

        c_step(&env);
    }

    float node_price = node_price_for_type(&env, node_type);
    float node_kw = node_kw_for_type(&env, node_type);
    printf("\tGPUs: %d\n", gpus);
    printf("\t8-GPU pods: %d\n", pods);
    printf("\tPower budget kW: %f\n", power_mw * 1000.0f);
    printf("\tEstimated compute load kW: %f\n", pods * node_kw);
    printf("\tCeiling: %f\n", env.episode_length * seller_spec.node_capacity[node_type] * node_price);
    printf("\tProfit: %f\n", env.log.profit / env.log.n);
    printf("\tExpense: %f\n", env.log.expense / env.log.n);
    printf("\tEpisode length: %f\n", env.log.episode_length / env.log.n);
    printf("\tEpisode return: %f\n", env.log.episode_return / env.log.n);
    printf("\tJobs completed: %f\n", env.log.jobs_completed / env.log.n);
    printf("\tCapacity used: %f\n", env.log.capacity_used / env.log.n);
    printf("\tN: %f\n", env.log.n);
    c_step(&env);
    free_buffers(&env);
    c_close(&env);
    printf("Finished %s\n", name);
}

void test_saudi() {
    test_power_site("Saudi GB200 site", GB200, 5000, 12.0f);
}

void test_los_alamos() {
    test_power_site("Los Alamos GH200 site", GH200, 2560, 3.0f);
}

void test_hut_8() {
    test_power_site("Hut 8 H100 site", H100, 1000, 1.43f);
}

int main() {
    test_determinism();
    test_sanity();
    test_storage();
    test_energy_production();
    test_bilateral_negotiation();
    test_200x5090();
    test_calcul();
    test_saudi();
    test_los_alamos();
    test_hut_8();
}
