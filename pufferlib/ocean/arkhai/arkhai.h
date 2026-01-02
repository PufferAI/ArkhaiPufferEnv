#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>
#include "raylib.h"

#define MAX_JOBS 100
#define NODE_TYPES 2

#define NUM_OBS 17
#define NUM_ACT 2

typedef struct {
    int price;
    int energy; } NodeSpec;
const int A100 = 0;
const int H100 = 1;

float NODE_PRICES[] = {0, 0};
float NODE_ENERGY_KW[] = {0, 0};

typedef struct {
    float score;
    float buyer_spend;
    float buyer_savings;
    float profit;
    float job_revenue;
    float energy_revenue;
    float energy_expense;
    float episode_length;
    float episode_return;
    float n;
} Log;

typedef struct {
    int nodes[NODE_TYPES];
    int space_tb;
    int start;
    int duration;
    bool active;
    float price; // Per tick
    int negotiations;
} Job;

typedef struct {
    int total;
    int free;
} Node;

typedef struct {
    float energy;
    float energy_gen;
    float energy_storage;
    float free_space_tb;
    float space_tb;
    Node nodes[NODE_TYPES];
    int sla;
    int location;
    float uptime;
    float latency;
    float bandwidth;
    float reputation;
} Cluster;

typedef struct {
    int node_capacity;
    float node_capacity_dr;
    int space_tb;
    float space_tb_dr;
    float energy_gen;
    float energy_gen_dr;
    float energy_storage;
    float energy_storage_dr;
    float buy_price;
    float buy_price_dr;
    float sell_price;
    float sell_price_dr;
    float job_efficiency;
    float job_efficiency_dr;
} ClusterSpec;

typedef struct {
    int type;
    Node nodes[NODE_TYPES];
    Cluster cluster;
    ClusterSpec cluster_spec;
    int space_tb;
    int free_space_tb;
    float energy_gen;
    float energy_storage;
    float energy;
    float job_revenue;
    float energy_revenue;
    float profit;
    float expense;
    float energy_expense;
    float prev_reward;
    float episode_return;
    Job jobs[MAX_JOBS];
    bool is_heuristic;
    bool is_buyer;
    Job request;
    int idx;
} Agent;

typedef struct {
    Log log;
    float* observations;
    int* actions;
    float* rewards;
    unsigned char* terminals;
    Agent* agents;
    int num_agent_sellers;
    int num_agent_buyers;
    int num_agents;
    int num_heuristic_sellers;
    int num_heuristic_buyers;
    bool buyer_is_heuristic;
    int tick;
    int episode_length;
    int max_job_duration;
    float job_duration_dr;
    int request_timeout;
    int max_nodes;
    float job_space_tb;
    float job_space_tb_dr;
    int max_space_tb;
    float max_energy_storage;
    float max_energy_gen;
    float buy_price_randomization;
    float sell_price_randomization;
    float job_efficiency_randomization;
    float reward_scale;
    float space_tb_price;
    float a100_node_price;
    float a100_node_energy_kw;
    float h100_node_price;
    float h100_node_energy_kw;
    float energy_demand_base;
    float energy_price_base;
    float energy_price_sensitivity;
    float energy_demand_threshold;
    float a1;
    float b1;
    float a2;
    float b2;
    float a3;
    float b3;
    int randomize_offset;
    int preset;
    Agent* serving;
} Arkhai;

float randf(float min, float max) {
    return min + ((float)rand()/(float)(RAND_MAX))*(max-min);
}

enum PRESET {
    NONE,
    DEFAULT,
    ENERGY_PRODUCER,
    STORAGE_CENTER,
    PREMIUM_HPC,
};

enum SIDE {
    SELLER,
    BUYER,
    BOTH,
};

float randomized(float base, float dr) {
    return randf(base*(1.0f-dr), base*(1.0f+dr));
}

void init_cluster(Cluster* cluster, ClusterSpec* spec) {
    cluster->energy_gen = randomized(spec->energy_gen, spec->energy_gen_dr);
    cluster->energy_storage = randomized(spec->energy_storage, spec->energy_storage_dr);
    cluster->space_tb = randomized(spec->space_tb, spec->space_tb_dr);
    for (int i=0; i<NODE_TYPES; i++) {
        cluster->nodes[i].total = randomized(spec->node_capacity, spec->node_capacity_dr);
        cluster->nodes[i].free = cluster->nodes[i].total;
    }
}

void apply_energy_producer_preset(Arkhai* env) {
    env->max_nodes = 0;
    env->max_space_tb = 0;
    env->max_energy_gen = 100;
    env->max_energy_storage = 1000;
}

void apply_storage_center_preset(Arkhai* env) {
    env->max_nodes = 0;
    env->max_energy_gen = 0;
    env->max_energy_storage = 0;
    env->max_space_tb = 10000;
    env->space_tb_price = 0.02;
}

// TODO: add sla, rep, etc
void apply_premium_hpc_preset(Arkhai* env) {
    env->a100_node_price *= 1.2;
    env->h100_node_price *= 1.2;
    env->space_tb_price *= 1.2;
}

Agent* select_buyer(Arkhai* env) {
    int num_buyers = env->num_agent_buyers + env->num_heuristic_buyers;
    int idxs[num_buyers];
    int i = 0;
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        if (agent->is_buyer) {
            idxs[i++] = agent_idx;
        }
    }

    // Shuffle
    for (int i=0; i<num_buyers; i++) {
        int j = rand()%(num_buyers - i) + i;
        int tmp = idxs[i];
        idxs[i] = idxs[j];
        idxs[j] = tmp;
    }
    return &env->agents[idxs[0]];
}


void init(Arkhai* env, ClusterSpec buyer_spec, ClusterSpec seller_spec) {
    env->num_agents = env->num_agent_sellers + env->num_agent_buyers + env->num_heuristic_sellers + env->num_heuristic_buyers;
    env->agents = calloc(env->num_agents, sizeof(Agent));

    if (env->preset == ENERGY_PRODUCER) {
        apply_energy_producer_preset(env);
    } else if (env->preset == STORAGE_CENTER) {
        apply_storage_center_preset(env);
    } else if (env->preset == PREMIUM_HPC) {
        apply_premium_hpc_preset(env);
    } else {
        int agent_idx = 0;
        for (int i=0; i<env->num_agent_sellers; i++) {
            env->agents[agent_idx].cluster_spec = seller_spec;
            env->agents[agent_idx].is_heuristic = false;
            env->agents[agent_idx].is_buyer = false;
            agent_idx++;
        }
        for (int i=0; i<env->num_agent_buyers; i++) {
            env->agents[agent_idx].cluster_spec = buyer_spec;
            env->agents[agent_idx].is_heuristic = false;
            env->agents[agent_idx].is_buyer = true;
            agent_idx++;
        }
        for (int i=0; i<env->num_heuristic_sellers; i++) {
            env->agents[agent_idx].cluster_spec = seller_spec;
            env->agents[agent_idx].is_heuristic = true;
            env->agents[agent_idx].is_buyer = false;
            agent_idx++;
        }
        for (int i=0; i<env->num_heuristic_buyers; i++) {
            env->agents[agent_idx].cluster_spec = buyer_spec;
            env->agents[agent_idx].is_heuristic = true;
            env->agents[agent_idx].is_buyer = true;
            agent_idx++;
        }
    }
 
    NODE_PRICES[A100] = env->a100_node_price;
    NODE_PRICES[H100] = env->h100_node_price;
    NODE_ENERGY_KW[A100] = env->a100_node_energy_kw;
    NODE_ENERGY_KW[H100] = env->h100_node_energy_kw;

    // Sanity checks. These are here because it is easy to mess up init
    assert(env->episode_length > 0);
    assert(env->max_job_duration > 0);
    assert(env->request_timeout > 0);
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        agent->idx = agent_idx;
        ClusterSpec* spec = &agent->cluster_spec;
        assert(spec->node_capacity >= 0);
        assert(spec->space_tb >= 0);
        assert(spec->energy_gen >= 0);
        assert(spec->energy_storage >= 0);
        assert(spec->buy_price >= 0);
        assert(spec->sell_price >= 0);
        assert(spec->job_efficiency >= 0);
        assert(spec->energy_gen_dr >= 0.0f);
        assert(spec->energy_storage_dr >= 0.0f);
        assert(spec->space_tb_dr >= 0.0f);
        assert(spec->node_capacity_dr >= 0.0f);
        assert(spec->buy_price_dr >= 0.0f);
        assert(spec->sell_price_dr >= 0.0f);
        assert(spec->job_efficiency_dr >= 0.0f);
    }

    assert(env->reward_scale > 0.0f);
    assert(env->a100_node_price > 0.0f);
    assert(env->a100_node_energy_kw > 0.0f);
    assert(env->h100_node_price > 0.0f);
    assert(env->h100_node_energy_kw > 0.0f);
    assert(env->energy_demand_base > 0.0f);
    assert(env->energy_price_base > 0.0f);
    assert(env->energy_price_sensitivity > 0.0f);
    assert(env->energy_demand_threshold > 0.0f);
    assert(env->a1 != 0.0f);
    assert(env->b1 != 0.0f);
    assert(env->a2 != 0.0f);
    assert(env->b2 != 0.0f);
    assert(env->a3 != 0.0f);
    assert(env->b3 != 0.0f);
    assert(env->randomize_offset == 0 || env->randomize_offset == 1);
    assert(
        env->preset == NONE ||
        env->preset == ENERGY_PRODUCER ||
        env->preset == STORAGE_CENTER ||
        env->preset == PREMIUM_HPC
    );
}

// Relaxed from milestone 1 to support
// storage centers with 0 nodes
bool job_is_valid(Job job) {
    for (int i=0; i<NODE_TYPES; i++) {
        if (job.nodes[i] < 0) {
            return false;
        }
    }
    return job.space_tb >= 0;
}

float job_price(Arkhai* env, Job* job) {
    float price = env->space_tb_price*job->space_tb;
    for (int i=0; i<NODE_TYPES; i++) {
        price += NODE_PRICES[i]*job->nodes[i];
    }
    return price;
}

// Jobs are generated based on the maximum capacity
// of the current environment configuration. This should
// be replaced with a more realistic distribution.
Job generate_request(Arkhai* env) {
    Job job = (Job) {
        .space_tb = 0,
        .start = env->tick,
        .duration = randomized(env->max_job_duration, env->job_duration_dr),
        .active = true,
        .negotiations = 0,
        .space_tb = randomized(env->job_space_tb, env->job_space_tb_dr)
    };
    for (int i=0; i<NODE_TYPES; i++) {
        job.nodes[i] = rand() % (env->max_nodes + 1);
    }
    job.price = job_price(env, &job);
    return job;
}

void compute_observations(Arkhai* env) {
    int i = 0;
    env->observations[i++] = (env->tick % 24) / 24.0f;

    // DO NOT ADD OR CHANGE INDEXING WITHOUT UPDATING BOTH BUYER AND SELLER CODE,
    // AS WELL AS THE OBS SIZE IN PYTHON. WE DO NOT HAVE A GOOD WAY TO AUTOMATICALLY
    // CHECK THIS. YOU WILL CAUSE SILENT MEMORY CORRUPTION OR SEGFAULTS.

    Job* request = &env->serving->request;
    int num_agents = env->num_agent_sellers + env->num_agent_buyers;
    for (int agent_idx=0; agent_idx<num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;

        for (int j=0; j<NODE_TYPES; j++) {
            env->observations[i++] = agent->nodes[j].total / ((float)env->max_nodes + 1);
            env->observations[i++] = agent->nodes[j].free / ((float)env->max_nodes + 1);
        }
        env->observations[i++] = agent->space_tb / ((float)env->max_space_tb + 1);
        env->observations[i++] = agent->free_space_tb / ((float)env->max_space_tb + 1);
        env->observations[i++] = agent->energy / ((float)agent->energy_storage + 1);
        env->observations[i++] = agent->energy_gen / ((float)agent->energy_gen + 1);
        env->observations[i++] = agent->energy_storage / ((float)agent->energy_storage + 1);

        for (int j=0; j<NODE_TYPES; j++) {
            env->observations[i++] = request->nodes[j] / ((float)env->max_nodes + 1);
        }
        env->observations[i++] = request->space_tb / ((float)env->max_space_tb + 1);
        env->observations[i++] = request->start / ((float)env->max_job_duration + 1);
        env->observations[i++] = request->duration / ((float)env->max_job_duration + 1);
        env->observations[i++] = request->negotiations / ((float)env->request_timeout + 1);
        env->observations[i++] = request->price / (job_price(env, request) + 1);
        env->observations[i++] = agent->prev_reward;
    }
}

void c_reset(Arkhai* env) {
    // This is for first-time reset. Staggering improves training stability.
    if (env->randomize_offset && env->tick == 0) {
        env->tick = rand()%env->episode_length;
    } else {
        env->tick = 0;
    }

    if (env->randomize_offset) {
        env->tick = rand()%env->max_job_duration;
    }


    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        init_cluster(&agent->cluster, &agent->cluster_spec);
        memset(agent->jobs, 0, MAX_JOBS*sizeof(Job));
        if (agent->is_buyer) {
            agent->request = generate_request(env);
        }
    }
    compute_observations(env);
}

bool can_accept_job(Arkhai* env, Agent* agent, Job* job) {
    Cluster* cluster = &agent->cluster;
    if (job->space_tb > cluster->free_space_tb) {
        return false;
    }
    for (int j=0; j<NODE_TYPES; j++) {
        if (job->nodes[j] > cluster->nodes[j].free) {
            return false;
        }
    }
    for (int i=0; i<MAX_JOBS; i++) {
        if (agent->jobs[i].active) {
            continue;
        }
        return true;
    }
    return false;
}

void accept_job(Arkhai* env, Job* job, int idx) {
    Agent* agent = env->agents + idx;
    Cluster* cluster = &agent->cluster;
    for (int i=0; i<MAX_JOBS; i++) {
        if (agent->jobs[i].active) {
            continue;
        }
        agent->jobs[i] = *job;
        for (int j=0; j<NODE_TYPES; j++) {
            cluster->nodes[j].free -= job->nodes[j];
        }
        cluster->free_space_tb -= job->space_tb;
        return;
    }
}

float job_kw(Arkhai* env, Job job) {
    float kw = 0.0f;
    for (int i=0; i<NODE_TYPES; i++) {
        kw += job.nodes[i]*NODE_ENERGY_KW[i];
    }
    float efficiency = 1.0f + randf(-env->job_efficiency_randomization, env->job_efficiency_randomization);
    return efficiency*kw;
}
 
// These two fns are simple responses for single-side sims. Ideally, you'd match them to
// the distribution of real-world demand to first order + heavy randomization.
float buyer_response(Arkhai* env, Job request, float offer_price) {
    float rng = 1.0f + randf(-env->buy_price_randomization, env->buy_price_randomization);
    return rng * job_price(env, &request);
}

float seller_response(Arkhai* env, Job request, float offer_price) {
    float rng = 1.0f + randf(-env->sell_price_randomization, env->sell_price_randomization);
    return rng * job_price(env, &request);
}

float calculate_price(float demand, float p0, float threshold, float c) {
    float excess = fmaxf(0.0f, demand - threshold);
    return p0 + c*powf(excess, 2.0f); // Quadratic for non-linear spike
}

float kw_price(Arkhai* env, float t) {
    float demand = env->energy_demand_base + (
        env->a1*cosf(2.0f*PI*t/24.0f) + env->b1*sinf(2.0f*PI*t/24.0f) +
        env->a2*cosf(4.0f*PI*t/24.0f) + env->b2*sinf(4.0f*PI*t/24.0f) +
        env->a3*cosf(6.0f*PI*t/24.0f) + env->b3*sinf(6.0f*PI*t/24.0f));
    float excess = fmaxf(0.0f, demand - env->energy_demand_threshold);
    float price_mwh = env->energy_price_base + env->energy_price_sensitivity*powf(excess, 2.0f);
    return 0.001f*price_mwh;
}

void clear_finished_jobs(Arkhai* env) {
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        Cluster* cluster = &agent->cluster;
        for (int i=0; i<MAX_JOBS; i++) {
            Job job = agent->jobs[i];
            if (!job.active) {
                continue;
            }
            if (env->tick < job.start + job.duration) {
                continue;
            }
            for (int j=0; j<NODE_TYPES; j++) {
                cluster->nodes[j].free += job.nodes[j];
            }
            cluster->free_space_tb += job.space_tb;
            memset(&agent->jobs[i], 0, sizeof(Job));
        }
    }
}

void update_jobs(Arkhai* env) {
    float reward = 0;
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        Cluster* cluster = &agent->cluster;
        for (int i=0; i<MAX_JOBS; i++) {
            Job job = agent->jobs[i];
            if (!job.active) {
                continue;
            }

            float kw = job_kw(env, job);
            if (cluster->energy > kw) {
                cluster->energy -= kw;
                kw = 0;
            } else {
                kw -= cluster->energy;
                cluster->energy = 0;
            }

            float energy_cost = kw*kw_price(env, env->tick);
            float profit = job.price - energy_cost;
            float buyer_savings = job_price(env, &job) - job.price;

            agent->profit += profit;
            agent->job_revenue += job.price;
            agent->energy_expense += energy_cost;

            /*
            if (env->side == SELLER) {
                reward += profit;
            } else {
                reward += buyer_savings;
            }
            */

        }
        
        env->rewards[0] += reward;
    }
} 

void c_step(Arkhai* env) {
    env->rewards[0] = 0;
    env->terminals[0] = 0;

    if (env->tick >= env->episode_length) {
        for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
            Agent* agent = env->agents + agent_idx;
            if (agent->is_heuristic) {

            } else {
                env->log.score += agent->profit;
                //env->log.buyer_spend += agent->buyer_spend;
                //env->log.buyer_savings += agent->buyer_savings;
                env->log.profit += agent->profit;
                env->log.energy_expense += agent->energy_expense;
                env->log.job_revenue += agent->job_revenue;
                env->log.energy_revenue += agent->energy_revenue;
                env->log.episode_length += env->tick;
                env->log.episode_return += agent->episode_return;
                env->log.n++;
                env->terminals[agent_idx] = 1;

            }
        }
        c_reset(env);
    }

    Agent* serving = env->serving;
    Job* request = &serving->request;
    request->negotiations++;
    
    assert(request != NULL);
    float base_price = job_price(env, request);
    int request_idx = serving->idx;

    // -0.2 -0.15 -0.1 -0.05 0.0f 0.05 0.1 0.15 0.2
    float price_mul = 1.0f + ((float)env->actions[2*request_idx] - 4.0f)/20.0f;
    float request_price = price_mul * base_price;

    int best_idx = -1;
    float best_response_price = 0.0f;
    float response_prices[env->num_agents];
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        if (agent_idx == request_idx) {
            response_prices[agent_idx] = 0.0f;
            continue;
        }
        Agent* agent = env->agents + agent_idx;
        if (!can_accept_job(env, agent, request)) {
            response_prices[agent_idx] = 0.0f;
            continue;
        }
        price_mul = 1.0f + ((float)env->actions[2*agent_idx+1] - 4.0f)/20.0f;
        float offer_price = price_mul * base_price;
        if (offer_price >= best_response_price) {
            best_idx = agent_idx;
            best_response_price = offer_price;
        }
    }
    if (best_response_price >= request_price) {
        accept_job(env, request, best_idx);
    } else if (request->negotiations < env->request_timeout) {
        compute_observations(env);
        return;
    }

    env->tick++;
    clear_finished_jobs(env);

    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        Cluster* cluster = &agent->cluster;
        cluster->energy += cluster->energy_gen;
        if (cluster->energy > cluster->energy_storage) {
            float diff = cluster->energy - cluster->energy_storage;
            cluster->energy = cluster->energy_storage;
            float profit = diff*kw_price(env, env->tick);
            //cluster->energy_revenue += profit;
            //cluster->profit += profit;
            /*
            if (env->side == SELLER) {
                env->rewards[0] += profit;
            }
            */
        }
    }

    update_jobs(env);

    // Sell energy
    /*
    if (env->actions[1] > 0 && env->side == SELLER) {
        float amt = 0.5f * env->energy;
        float profit = amt*kw_price(env, env->tick);
        env->energy_revenue += profit;
        env->profit += profit;
        env->energy -= amt;
        env->rewards[0] += profit;
    }
    */

    // Scale and clip rewards
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        float reward = env->rewards[agent_idx];
        reward *= env->reward_scale;
        assert(reward >= -1.0f);
        assert(reward <= 1.0f);
        env->rewards[agent_idx] = reward;

        agent->episode_return += reward;
        agent->request = generate_request(env);
        agent->prev_reward = reward;
    }
    compute_observations(env);
}

const Color PUFF_RED = (Color){187, 0, 0, 255};
const Color PUFF_CYAN = (Color){0, 187, 187, 255};
const Color PUFF_WHITE = (Color){241, 241, 241, 241};
const Color PUFF_BACKGROUND = (Color){6, 24, 24, 255};

void c_render(Arkhai* env) {
    if (!IsWindowReady()) {
        InitWindow(1080, 720, "PufferLib Arkhai");
        SetTargetFPS(5);
    }

    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }

    BeginDrawing();
    ClearBackground(PUFF_BACKGROUND);
    EndDrawing();
}

void c_close(Arkhai* env) {
    if (IsWindowReady()) {
        CloseWindow();
    }
}
