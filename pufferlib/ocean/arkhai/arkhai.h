#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <float.h>
#include "raylib.h"

#define MAX_JOBS 100

#define NUM_OBS 67
#define NUM_ACT 2

typedef struct {
    int price;
    int kwh_storage; } NodeSpec;

const int A100 = 0;
const int H100 = 1;
const int R5090 = 2;
const int GH200 = 3;
const int GB200 = 4;
#define NODE_TYPES 5
float NODE_PRICES[] = {0, 0, 0, 0, 0};
float NODE_ENERGY_KW[] = {0, 0, 0, 0, 0};

// Whenever you call vec_log, PufferLib will
// sum all fields in Log across all env instances per-core
// and divide by n. All fields must be floats, and n must
// be the last field.
typedef struct {
    float score;
    float expense;
    float profit;
    float energy_revenue;
    float energy_expense;
    float episode_length;
    float episode_return;
    float jobs_completed;
    float capacity_used;
    float n;
} Log;

typedef struct {
    int nodes[NODE_TYPES];
    float tb_usage;
    int duration;
    int start;
    bool active;
    float price; // Per tick
    int negotiations;
    float efficiency; // Should be a joint property of job and cluster...hard to model
} Job;

typedef struct {
    int total;
    int free;
} Node;

typedef struct {
    Node nodes[NODE_TYPES];
    float tb_capacity;
    float tb_usage;
    float kwh_storage;
    float kwh_capacity;
    float kw_generation;
    // Placeholders are here but these are not really meaningful without some
    // sort of requirement or reward directive on job requests
    int sla;
    int location;
    float uptime;
    float latency;
    float bandwidth;
    float reputation;
} Cluster;

// Different clusters can have different specifications with noise.
// Clusters are sampled from a ClusterSpec params + domain randomization (dr).
typedef struct {
    int node_capacity[NODE_TYPES];
    float node_capacity_dr[NODE_TYPES];
    int tb_capacity;
    float tb_capacity_dr;
    float kwh_capacity;
    float kwh_capacity_dr;
    float kw_generation;
    float kw_generation_dr;
} ClusterSpec;

// Agents can be buyers or sellers, heuristic or RL
typedef struct {
    ClusterSpec cluster_spec;
    Cluster cluster;
    Job jobs[MAX_JOBS];
    Job request;
    float job_revenue;
    float compute_expense;
    float energy_revenue;
    float energy_expense;
    float prev_reward;
    float episode_return;
    float profit_this_tick;
    int filled_jobs;
    int jobs_completed;
    float capacity_used;
    bool is_heuristic;
    bool is_buyer;
} Agent;

typedef struct {
    // This chunk is all required puffer api
    Log log;
    float* observations;
    int* actions;
    float* rewards;
    unsigned char* terminals;
    // ... until here. These pointers get set to chunks of
    // shared memory by env_binding.h
    Agent* agents;
    int ai_sellers;
    int ai_buyers;
    int scripted_buyers;
    int scripted_sellers;
    int num_agents;
    int tick;
    int episode_length;
    int request_timeout;
    int job_nodes[NODE_TYPES];
    float job_nodes_dr[NODE_TYPES];
    int job_duration;
    float job_duration_dr;
    float job_tb_usage;
    float job_tb_usage_dr;
    float job_efficiency;
    float job_efficiency_dr;
    float scripted_buy_price;
    float scripted_buy_price_dr;
    float scripted_sell_price;
    float scripted_sell_price_dr;
    float reward_scale;
    float tb_price;
    float a100_price;
    float a100_kw;
    float h100_price;
    float h100_kw;
    float r5090_price;
    float r5090_kw;
    float gh200_price;
    float gh200_kw;
    float gb200_price;
    float gb200_kw;
    float energy_demand_base;
    float kwh_price_base;
    float kwh_price_sensitivity;
    float kwh_demand_threshold;
    float a1;
    float b1;
    float a2;
    float b2;
    float a3;
    float b3;
    int randomize_offset;
    int preset;
    int serving;
    int serving_seller;
    bool debug;
} Arkhai;

// I was playing with having "presets" for different training configs.
// It's a bit fiddly though. There are way more env knobs than when I introduced this.
enum PRESET {
    NONE,
    DEFAULT,
    ENERGY_PRODUCER,
    STORAGE_CENTER,
    PREMIUM_HPC,
};

/*
void apply_kwh_storage_producer_preset(ClusterSpec* spec) {
    env->max_nodes = 0;
    env->max_tb_usage = 0;
    env->max_kw_generation = 100;
    env->max_kwh_capacity = 1000;
}

void apply_storage_center_preset(Arkhai* env) {
    env->max_nodes = 0;
    env->max_kw_generation = 0;
    env->max_kwh_capacity = 0;
    env->max_tb_usage = 10000;
    env->tb_usage_price = 0.02;
}

// TODO: add sla, rep, etc
void apply_premium_hpc_preset(Arkhai* env) {
    env->a100_price *= 1.2;
    env->h100_price *= 1.2;
    env->tb_usage_price *= 1.2;
}
*/

enum SIDE {
    SELLER,
    BUYER,
    BOTH,
};

bool can_accept_job(Arkhai* env, Agent* agent, Job* job);

float randf(float min, float max) {
    return min + ((float)rand()/(float)(RAND_MAX))*(max-min);
}

// Simple uniform randomization with dr
float randomized(float base, float dr) {
    return randf(base*(1.0f-dr), base*(1.0f+dr));
}

// I didn't know how you would want to price energy so I just gave you
// some Fourier components and fit something that looks roughly like the
// wikipedia graphs.
float kw_price(Arkhai* env, float t) {
    float demand = env->energy_demand_base + (
        env->a1*cosf(2.0f*PI*t/24.0f) + env->b1*sinf(2.0f*PI*t/24.0f) +
        env->a2*cosf(4.0f*PI*t/24.0f) + env->b2*sinf(4.0f*PI*t/24.0f) +
        env->a3*cosf(6.0f*PI*t/24.0f) + env->b3*sinf(6.0f*PI*t/24.0f));
    float excess = fmaxf(0.0f, demand - env->kwh_demand_threshold);
    return env->kwh_price_base + env->kwh_price_sensitivity*powf(excess, 2.0f);
}

void init_cluster(Cluster* cluster, ClusterSpec* spec) {
    *cluster = (Cluster){0};
    cluster->kw_generation = randomized(spec->kw_generation, spec->kw_generation_dr);
    cluster->kwh_capacity = randomized(spec->kwh_capacity, spec->kwh_capacity_dr);
    cluster->tb_capacity = randomized(spec->tb_capacity, spec->tb_capacity_dr);
    for (int i=0; i<NODE_TYPES; i++) {
        int capacityy = spec->node_capacity[i];
        float capacity_dr = spec->node_capacity_dr[i];
        cluster->nodes[i].total = randomized(capacityy, capacity_dr);
        cluster->nodes[i].free = cluster->nodes[i].total;
    }
}

void init(Arkhai* env, ClusterSpec buyer_spec, ClusterSpec seller_spec) {
    int num_buyers = env->ai_buyers + env->scripted_buyers;
    assert(num_buyers > 0);

    int num_sellers = env->ai_sellers + env->scripted_sellers;
    assert(num_sellers > 0);
    
    env->num_agents = num_buyers + num_sellers;
    env->agents = calloc(env->num_agents, sizeof(Agent));
    env->tick = 0;

    // Note storage order: RL agents first, then heuristic agents
    int agent_idx = 0;
    for (int i=0; i<env->ai_sellers; i++) {
        env->agents[agent_idx].cluster_spec = seller_spec;
        env->agents[agent_idx].is_heuristic = false;
        env->agents[agent_idx].is_buyer = false;
        agent_idx++;
    }
    for (int i=0; i<env->ai_buyers; i++) {
        env->agents[agent_idx].cluster_spec = buyer_spec;
        env->agents[agent_idx].is_heuristic = false;
        env->agents[agent_idx].is_buyer = true;
        agent_idx++;
    }
    for (int i=0; i<env->scripted_sellers; i++) {
        env->agents[agent_idx].cluster_spec = seller_spec;
        env->agents[agent_idx].is_heuristic = true;
        env->agents[agent_idx].is_buyer = false;
        agent_idx++;
    }
    for (int i=0; i<env->scripted_buyers; i++) {
        env->agents[agent_idx].cluster_spec = buyer_spec;
        env->agents[agent_idx].is_heuristic = true;
        env->agents[agent_idx].is_buyer = true;
        agent_idx++;
    }

    NODE_PRICES[A100] = env->a100_price;
    NODE_PRICES[H100] = env->h100_price;
    NODE_PRICES[R5090] = env->r5090_price;
    NODE_PRICES[GH200] = env->gh200_price;
    NODE_PRICES[GB200] = env->gb200_price;
    NODE_ENERGY_KW[A100] = env->a100_kw;
    NODE_ENERGY_KW[H100] = env->h100_kw;
    NODE_ENERGY_KW[R5090] = env->r5090_kw;
    NODE_ENERGY_KW[GH200] = env->gh200_kw;
    NODE_ENERGY_KW[GB200] = env->gb200_kw;

    // Sanity checks. These are here because it is easy to mess up init
    assert(env->ai_sellers >= 0);
    assert(env->ai_buyers >= 0);
    assert(env->scripted_buyers >= 0);
    assert(env->scripted_sellers >= 0);
    assert(env->num_agents > 0);
    assert(env->episode_length > 0);
    assert(env->request_timeout >= 0);
    assert(env->job_duration > 0);
    assert(env->job_duration_dr >= 0.0f);
    assert(env->job_tb_usage >= 0);
    assert(env->job_tb_usage_dr >= 0.0f);
    assert(env->scripted_sell_price > 0.0f);
    assert(env->scripted_sell_price_dr >= 0.0f);
    assert(env->scripted_buy_price > 0.0f);
    assert(env->scripted_buy_price_dr >= 0.0f);
    assert(env->reward_scale > 0.0f);
    assert(env->tb_price >= 0.0f);
    assert(env->a100_price >= 0.0f);
    assert(env->a100_kw > 0.0f);
    assert(env->h100_price >= 0.0f);
    assert(env->h100_kw > 0.0f);
    assert(env->r5090_price >= 0.0f);
    assert(env->r5090_kw > 0.0f);
    assert(env->gh200_price >= 0.0f);
    assert(env->gh200_kw > 0.0f);
    assert(env->gb200_price >= 0.0f);
    assert(env->gb200_kw > 0.0f);
    assert(env->energy_demand_base >= 0.0f);
    assert(env->kwh_price_base >= 0.0f);
    assert(env->kwh_price_sensitivity >= 0.0f);
    assert(env->kwh_demand_threshold >= 0.0f);

    assert(env->randomize_offset == 0 || env->randomize_offset == 1);
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        ClusterSpec* spec = &env->agents[agent_idx].cluster_spec;
        for (int i=0; i<NODE_TYPES; i++) {
            assert(spec->node_capacity[i] >= 0);
            assert(spec->node_capacity_dr[i] >= 0.0f);
        }
        assert(spec->tb_capacity >= 0);
        assert(spec->tb_capacity_dr >= 0.0f);
        assert(spec->kwh_capacity >= 0);
        assert(spec->kwh_capacity_dr >= 0.0f);
        assert(spec->kw_generation >= 0);
        assert(spec->kw_generation_dr >= 0.0f);
    }

}

// Used to select a random buy order
int select_buyer(Arkhai* env) {
    int num_buyers = env->scripted_buyers + env->ai_buyers;
    int idxs[num_buyers];
    int i = 0;
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        if (agent->is_buyer) {
            idxs[i++] = agent_idx;
        }
    }

    int idx_idx = rand()%num_buyers;
    return idxs[idx_idx];
}

int select_seller(Arkhai* env, Job* request) {
    int num_sellers = env->scripted_sellers + env->ai_sellers;
    int idxs[num_sellers];
    int i = 0;
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        if (agent->is_buyer || !can_accept_job(env, agent, request)) {
            continue;
        }
        idxs[i++] = agent_idx;
    }

    if (i == 0) {
        return -1;
    }
    int idx_idx = rand()%i;
    return idxs[idx_idx];
}

void sanity_check(Job job) {
    assert(job.tb_usage >= 0);
    for (int i=0; i<NODE_TYPES; i++) {
        assert(job.nodes[i] >= 0);
    }
}

float job_price(Arkhai* env, Job* job) {
    float price = env->tb_price*job->tb_usage;
    for (int i=0; i<NODE_TYPES; i++) {
        price += NODE_PRICES[i]*job->nodes[i];
    }
    return price;
}

// Random job request, based on job spec settings. If no jobs are
// filling, check that your seller has enough capacity
Job generate_request(Arkhai* env) {
    Job job = (Job) {
        .tb_usage = randomized(env->job_tb_usage, env->job_tb_usage_dr),
        .duration = randomized(env->job_duration, env->job_duration_dr),
        .start = env->tick,
        .active = true,
        .negotiations = 0,
    };
    for (int i=0; i<NODE_TYPES; i++) {
        int nodes = env->job_nodes[i];
        float nodes_dr = env->job_nodes_dr[i];
        job.nodes[i] = randomized(nodes, nodes_dr);
    }
    job.price = job_price(env, &job);
    return job;
}

void write_agent_observations(Arkhai* env, Agent* agent, Job* request, bool full_info, int* i) {
    Cluster* cluster = &agent->cluster;
    if (full_info) {
        for (int j=0; j<NODE_TYPES; j++) {
            env->observations[(*i)++] = cluster->nodes[j].total / ((float)env->job_nodes[j] + 1);
            env->observations[(*i)++] = cluster->nodes[j].free / ((float)env->job_nodes[j] + 1);
        }
        env->observations[(*i)++] = cluster->tb_usage / ((float)env->job_tb_usage + 1);
        env->observations[(*i)++] = cluster->tb_capacity / ((float)env->job_tb_usage + 1);
        // TODO: these should be divided by global capacities
        env->observations[(*i)++] = cluster->kwh_storage / ((float)cluster->kwh_capacity + 1);
        env->observations[(*i)++] = cluster->kwh_capacity / ((float)cluster->kwh_capacity + 1);
        env->observations[(*i)++] = cluster->kw_generation / ((float)cluster->kwh_capacity + 1);
    } else {
        // Capacity and energy generation are private counterparty information.
        for (int j=0; j<NODE_TYPES; j++) {
            env->observations[(*i)++] = 0.0f;
            env->observations[(*i)++] = 0.0f;
        }
        env->observations[(*i)++] = 0.0f;
        env->observations[(*i)++] = 0.0f;
        env->observations[(*i)++] = 0.0f;
        env->observations[(*i)++] = 0.0f;
        env->observations[(*i)++] = 0.0f;
    }

    // These fields are empty placeholders for now. Normalize them
    // appropriately here once their actual ranges and semantics are set.
    env->observations[(*i)++] = cluster->sla;
    env->observations[(*i)++] = cluster->location;
    env->observations[(*i)++] = cluster->uptime;
    env->observations[(*i)++] = cluster->latency;
    env->observations[(*i)++] = cluster->bandwidth;
    env->observations[(*i)++] = cluster->reputation;

    if (request != NULL) {
        for (int j=0; j<NODE_TYPES; j++) {
            env->observations[(*i)++] = request->nodes[j] / ((float)env->job_nodes[j] + 1);
        }
        env->observations[(*i)++] = request->tb_usage / ((float)env->job_tb_usage + 1);
        env->observations[(*i)++] = request->start / ((float)env->job_duration + 1);
        env->observations[(*i)++] = request->duration / ((float)env->job_duration + 1);
        env->observations[(*i)++] = request->negotiations / ((float)env->request_timeout + 1);
        env->observations[(*i)++] = request->price / (job_price(env, request) + 1);
    } else {
        for (int j=0; j<10; j++) {
            env->observations[(*i)++] = 0.0f;
        }
    }

    env->observations[(*i)++] = full_info ? agent->prev_reward : 0.0f;
}

void compute_observations(Arkhai* env) {
    int i = 0;

    // DO NOT ADD OR CHANGE INDEXING WITHOUT UPDATING BOTH BUYER AND SELLER CODE,
    // AS WELL AS THE OBS SIZE IN PYTHON. WE DO NOT HAVE A GOOD WAY TO AUTOMATICALLY
    // CHECK THIS. YOU WILL CAUSE SILENT MEMORY CORRUPTION OR SEGFAULTS.
    Agent* buyer = &env->agents[env->serving];
    Agent* seller = env->serving_seller >= 0 ? &env->agents[env->serving_seller] : NULL;
    Job* request = &buyer->request;
    int num_agents = env->ai_sellers + env->ai_buyers;
    for (int agent_idx=0; agent_idx<num_agents; agent_idx++) {
        env->observations[i++] = (env->tick % 24) / 24.0f;
        env->observations[i++] = env->tick / (float)env->episode_length;
        env->observations[i++] = kw_price(env, env->tick);

        write_agent_observations(env, buyer, request, agent_idx == env->serving, &i);
        if (seller != NULL) {
            write_agent_observations(env, seller, NULL, agent_idx == env->serving_seller, &i);
        } else {
            for (int j=0; j<32; j++) {
                env->observations[i++] = 0.0f;
            }
        }
    }
}

void c_reset(Arkhai* env) {
    // This is for first-time reset only. Staggering improves training stability.
    // Forgetting to disable this is a great way to break your evals.
    if (env->randomize_offset && env->tick == 0) {
        env->tick = 0 + rand()%env->episode_length;
    } else {
        env->tick = 0;
    }

    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        agent->job_revenue = 0.0f;
        agent->compute_expense = 0.0f;
        agent->energy_revenue = 0.0f;
        agent->energy_expense = 0.0f;
        agent->prev_reward = 0.0f;
        agent->episode_return = 0.0f;
        agent->filled_jobs = 0;
        agent->jobs_completed = 0;
        agent->capacity_used = 0.0f;
        init_cluster(&agent->cluster, &agent->cluster_spec);
        memset(agent->jobs, 0, MAX_JOBS*sizeof(Job));
        if (agent->is_buyer) {
            agent->request = generate_request(env);
        }
    }
    env->serving = select_buyer(env);
    env->serving_seller = select_seller(env, &env->agents[env->serving].request);
    compute_observations(env);
}

bool can_accept_job(Arkhai* env, Agent* agent, Job* job) {
    Cluster* cluster = &agent->cluster;
    if (job->tb_usage > cluster->tb_capacity) {
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

// Mark nodes/tb as used on accept
void accept_job(Arkhai* env, Job* job, int idx) {
    assert(idx >= 0 && idx < env->num_agents);
    Agent* agent = &env->agents[idx];
    Cluster* cluster = &agent->cluster;
    for (int i=0; i<MAX_JOBS; i++) {
        if (agent->jobs[i].active) {
            continue;
        }
        agent->jobs[i] = *job;
        for (int j=0; j<NODE_TYPES; j++) {
            cluster->nodes[j].free -= job->nodes[j];
        }
        //printf("Accepted job on tick %d . Remaining 5090 nodes: %d\n", env->tick, cluster->nodes[R5090].free);
        cluster->tb_capacity -= job->tb_usage;
        return;
    }
}

// We assume jobs can have an efficiency modifier. You should set
// job_efficiency < 1 to compensate if you use this.
float job_kw(Arkhai* env, Job job) {
    float kw = 0.0f;
    for (int i=0; i<NODE_TYPES; i++) {
        kw += job.nodes[i]*NODE_ENERGY_KW[i];
    }
    float efficiency = randomized(env->job_efficiency, env->job_efficiency_dr);
    return efficiency*kw;
}
 
// These two fns are simple responses for single-side sims. Ideally, you'd match them to
// the distribution of real-world demand to first order + heavy randomization.
float buyer_response(Arkhai* env, Job request, float offer_price) {
    float rng = randomized(env->scripted_buy_price, env->scripted_buy_price_dr);
    return rng * job_price(env, &request);
}

float seller_response(Arkhai* env, Job request, float offer_price) {
    float rng = randomized(env->scripted_sell_price, env->scripted_sell_price_dr);
    return rng * job_price(env, &request);
}

float price_action_multiplier(int action) {
    assert(action >= 1 && action <= 9);
    // Action 0 is reject. Actions 1-9 map to the original +/- 20% buckets.
    return 1.0f + ((float)action - 5.0f)/20.0f;
}

float calculate_price(float demand, float p0, float threshold, float c) {
    float excess = fmaxf(0.0f, demand - threshold);
    return p0 + c*powf(excess, 2.0f); // Quadratic for non-linear spike
}

// Clear/update fns will bottleneck perf eventually with enough agents/jobs
// and we will have to do something smarter, but it is fast for now.
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
            cluster->tb_capacity += job.tb_usage;
            agent->jobs_completed++;
            memset(&agent->jobs[i], 0, sizeof(Job));
        }
    }
}

void update_capacity_used(Arkhai* env) {
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        if (agent->is_buyer) {
            continue;
        }

        int total_gpus = 0;
        int used_gpus = 0;
        Cluster* cluster = &agent->cluster;
        for (int i=0; i<NODE_TYPES; i++) {
            total_gpus += cluster->nodes[i].total;
            used_gpus += cluster->nodes[i].total - cluster->nodes[i].free;
        }

        if (total_gpus == 0) {
            continue;
        }
        agent->capacity_used += used_gpus / ((float)total_gpus * env->episode_length);
    }
}

void update_jobs(Arkhai* env) {
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        Cluster* cluster = &agent->cluster;
        for (int i=0; i<MAX_JOBS; i++) {
            if (agent->is_buyer) {
                continue;
            }

            Job job = agent->jobs[i];
            if (!job.active) {
                continue;
            }

            float kw = job_kw(env, job);
            if (cluster->kwh_storage > kw) {
                cluster->kwh_storage -= kw;
                kw = 0;
            } else {
                kw -= cluster->kwh_storage;
                cluster->kwh_storage = 0;
            }

            // Jobs incur energy expense as it is used. Pricing is upfront.
            float energy_expense = kw*kw_price(env, env->tick);
            agent->energy_expense += energy_expense;
            agent->profit_this_tick -= energy_expense;
        }
    }
} 

void c_step(Arkhai* env) {
    int ai_agents = env->ai_sellers + env->ai_buyers;
    memset(env->rewards, 0, ai_agents*sizeof(float));
    memset(env->terminals, 0, ai_agents*sizeof(unsigned char));

    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = env->agents + agent_idx;
        agent->profit_this_tick = 0.0f;
    }

    // We have an episode timeout. You need this to resample domain randomization.
    // It can also mask degenerate states though. We should test some longer runs.
    if (env->tick >= env->episode_length) {
        for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
            Agent* agent = env->agents + agent_idx;
            if (env->debug) {
                printf("Agent %d\n", agent_idx);
                printf("\tIs Heuristic: %d\n", agent->is_heuristic);
                printf("\tIs Buyer: %d\n", agent->is_buyer);
                printf("\tJob Revenue: %f\n", agent->job_revenue);
                printf("\tCompute Expense: %f\n", agent->compute_expense);
                printf("\tFilled Jobs: %d\n", agent->filled_jobs);
                printf("\tJobs Completed: %d\n", agent->jobs_completed);
                printf("\tCapacity Used: %f\n", agent->capacity_used);
                printf("\tEnergy Revenue: %f\n", agent->energy_revenue);
                printf("\tEnergy Expense: %f\n", agent->energy_expense);
                printf("\tEpisode Return: %f\n", agent->episode_return);
            }

            if (agent->is_heuristic) {
                continue;
            }

            float job_profit = agent->job_revenue - agent->compute_expense;
            float energy_profit = agent->energy_revenue - agent->energy_expense;
            env->log.profit += job_profit + energy_profit;
            env->log.expense += agent->compute_expense + agent->energy_expense;
            env->log.energy_revenue += agent->energy_revenue;
            env->log.energy_expense += agent->energy_expense;
            env->log.score += job_profit + energy_profit;
            env->log.episode_length += env->tick;
            env->log.episode_return += agent->episode_return;
            env->log.jobs_completed += agent->jobs_completed;
            env->log.capacity_used += agent->capacity_used;
            env->log.n++;
            env->terminals[agent_idx] = 1;
        }
        c_reset(env);
        return;
    }

    int serving = env->serving;
    Job* request = &env->agents[serving].request;
    request->negotiations++;
    
    assert(request != NULL);
    float base_price = job_price(env, request);
    int request_idx = serving;

    Agent* buyer = &env->agents[request_idx];
    float request_price;
    if (buyer->is_heuristic) {
        request_price = buyer_response(env, *request, base_price);
    } else if (env->actions[2*request_idx] == 0) {
        request_price = 0.0f;
    } else {
        // I am discretizing the action space to +/- 20% of base price estimate in buckets of 5%
        // This does not need to be uniform, and we can adjust it for finer pricing. Not too
        // many buckets though.
        // -0.2 -0.15 -0.1 -0.05 0.0f 0.05 0.1 0.15 0.2
        float price_mul = price_action_multiplier(env->actions[2*request_idx]);
        request_price = price_mul * base_price;
    }

    int seller_idx = env->serving_seller;
    Agent* seller = seller_idx >= 0 ? &env->agents[seller_idx] : NULL;
    bool exists_valid_seller = seller != NULL && can_accept_job(env, seller, request);
    float seller_offer_price = FLT_MAX;
    if (exists_valid_seller) {
        if (seller->is_heuristic) {
            seller_offer_price = seller_response(env, *request, base_price);
        } else if (env->actions[2*seller_idx] == 0) {
            seller_offer_price = FLT_MAX;
        } else {
            float price_mul = price_action_multiplier(env->actions[2*seller_idx]);
            seller_offer_price = price_mul * base_price;
        }
    }

    if (!exists_valid_seller) {
        // Skip negotiation
    } else if (seller_offer_price <= request_price) {
        // The selected seller wins the job
        accept_job(env, request, seller_idx);

        // Full duration used for reward. Clipped duration used for logs.
        // This prevents the agent from exploiting terminal bounds...
        // Somewhat. It gets to "ignore" energy expense past the end of the episode.
        // We should probably come up with a way around this. But energy expense is
        // actually not that high. It looks like most of pricing is capex on chips.
        float duration = request->duration;
        float clipped_duration = duration;
        if (env->episode_length - env->tick < duration) {
            clipped_duration = env->episode_length - env->tick;
        }

        float buyer_revenue = job_price(env, request);
        float buyer_expense = seller_offer_price;
        buyer->job_revenue += buyer_revenue*clipped_duration;
        buyer->compute_expense += buyer_expense*clipped_duration;
        buyer->filled_jobs++;
        if (!buyer->is_heuristic) {
            buyer->profit_this_tick += (buyer_revenue - buyer_expense)*duration;
        }

        float seller_revenue = seller_offer_price;
        seller->job_revenue += seller_revenue*clipped_duration;
        //printf("recognizing seller revenue %f\n", seller_revenue*clipped_duration);
        // Energy is recognized as it is incurred.

        seller->filled_jobs++;
        if (!seller->is_heuristic) {
            seller->profit_this_tick += seller_revenue*duration;
        }
    } else if (request->negotiations < env->request_timeout) {
        compute_observations(env);
        return;
    }

    update_capacity_used(env);
    env->tick++;
    clear_finished_jobs(env);
    buyer->request = generate_request(env);
    env->serving_seller = select_seller(env, &buyer->request);

    // Assume energy over capacity is sold at the current market price
    for (int agent_idx=0; agent_idx<env->num_agents; agent_idx++) {
        Agent* agent = &env->agents[agent_idx];
        Cluster* cluster = &agent->cluster;
        cluster->kwh_storage += cluster->kw_generation;
        if (cluster->kwh_storage <= cluster->kwh_capacity) {
            continue;
        }
        float diff = cluster->kwh_storage - cluster->kwh_capacity;
        cluster->kwh_storage = cluster->kwh_capacity;
        float energy_revenue = diff*kw_price(env, env->tick);
        agent->energy_revenue += energy_revenue;
        if (!agent->is_heuristic) {
            agent->profit_this_tick += energy_revenue;
        }
    }

    update_jobs(env);

    for (int agent_idx=0; agent_idx<ai_agents; agent_idx++) {
        Agent* agent = &env->agents[agent_idx];
        Cluster* cluster = &agent->cluster;

        int energy_atn = env->actions[2*agent_idx + 1];
        if (energy_atn < 4) {
            float sell_frac = (energy_atn + 1) / 4.0f; // 0.25, 0.5, 0.75, 1.0
            float amt = sell_frac * cluster->kwh_storage;
            float price = amt*kw_price(env, env->tick);
            agent->profit_this_tick += price;
            agent->energy_revenue += price;
            cluster->kwh_storage -= amt;
        } else if (energy_atn > 4) {
            float buy_frac = (energy_atn - 4) / 4.0f; // 0.25, 0.5, 0.75, 1.0
            float amt = buy_frac * (cluster->kwh_capacity - cluster->kwh_storage);
            float price = amt*kw_price(env, env->tick);
            agent->profit_this_tick -= price;
            agent->energy_expense += price;
            cluster->kwh_storage += amt;
        }

        // Scale rewards
        float reward = env->reward_scale * agent->profit_this_tick;
        agent->episode_return += reward;
        env->rewards[agent_idx] = reward;
        agent->prev_reward = reward;
    }

    compute_observations(env);
}

// Placeholder. There isn't a renderer at the moment.
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
    free(env->agents);
}
