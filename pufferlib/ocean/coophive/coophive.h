#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>
#include "raylib.h"

#define MAX_JOBS 100
#define NODE_TYPES 2

typedef struct {
    int price;
    int energy; } NodeSpec;
const int A100 = 0;
const int H100 = 1;

float NODE_PRICES[] = {0, 0};
float NODE_ENERGY_KW[] = {0, 0};

typedef struct {
    float score;
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
    Log log;
    float* observations;
    int* actions;
    float* rewards;
    unsigned char* terminals;
    int tick;
    Node nodes[NODE_TYPES];
    int space_tb;
    int free_space_tb;
    float energy;
    float job_revenue;
    float energy_revenue;
    float profit;
    float prev_reward;
    float energy_expense;
    float episode_return;
    Job request;
    Job jobs[MAX_JOBS];
    int episode_length;
    int max_job_duration;
    int request_timeout;
    float energy_gen;
    float energy_storage;
    int max_nodes;
    float max_space_tb;
    float buy_price_randomization;
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
} CoopHive;

float randf(float min, float max) {
    return min + ((float)rand()/(float)(RAND_MAX))*(max-min);
}

void init(CoopHive* env) {
    NODE_PRICES[A100] = env->a100_node_price;
    NODE_PRICES[H100] = env->h100_node_price;
    NODE_ENERGY_KW[A100] = env->a100_node_energy_kw;
    NODE_ENERGY_KW[H100] = env->h100_node_energy_kw;

    // Sanity checks. These are here because it is easy to mess up init
    assert(env->episode_length > 0);
    assert(env->max_job_duration > 0);
    assert(env->request_timeout > 0);
    assert(env->max_nodes > 0);
    assert(env->max_space_tb > 0);
    assert(env->buy_price_randomization >= 0.0f);
    assert(env->job_efficiency_randomization >= 0.0f);
    assert(env->reward_scale > 0.0f);
    assert(env->space_tb_price >= 0.0f);
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
}

Job generate_request(CoopHive* env) {
    for (int i=0; i<NODE_TYPES; i++) {
        if (env->nodes[i].free == 0) {
            return (Job){0};
        }
    }
    if (env->free_space_tb == 0) {
        return (Job){0};
    }
    Job job = (Job) {
        .space_tb = rand()%env->space_tb + 1,
        .start = env->tick,
        .duration = rand()%env->max_job_duration + 1,
        .active = true
    };
    for (int i=0; i<NODE_TYPES; i++) {
        job.nodes[i] = rand()%env->nodes[i].total + 1;
    }
    return job;
}

bool job_is_valid(Job job) {
    bool any_nodes = false;
    for (int i=0; i<NODE_TYPES; i++) {
        if (job.nodes[i] < 0) {
            return false;
        }
        if (job.nodes[i] > 0) {
            any_nodes = true;
        }
    }
    if (!any_nodes) {
        return false;
    }
    return job.space_tb > 0;
}

float job_price(CoopHive* env, Job job) {
    float price = env->space_tb_price*job.space_tb;
    for (int i=0; i<NODE_TYPES; i++) {
        price += NODE_PRICES[i]*job.nodes[i];
    }
    return price;
}

void compute_observations(CoopHive* env) {
    int i = 0;
    for (int j=0; j<NODE_TYPES; j++) {
        env->observations[i++] = env->nodes[j].total / (float)env->max_nodes;
        env->observations[i++] = env->nodes[j].free / (float)env->max_nodes;
    }
    env->observations[i++] = env->space_tb / env->max_space_tb;
    env->observations[i++] = env->free_space_tb / env->max_space_tb;
    env->observations[i++] = env->energy / env->energy_storage;
    env->observations[i++] = env->energy_gen / env->energy_gen;
    env->observations[i++] = env->energy_storage / env->energy_storage;
    for (int j=0; j<NODE_TYPES; j++) {
        env->observations[i++] = env->request.nodes[j] / (float)env->max_nodes;
    }
    env->observations[i++] = env->request.space_tb / env->max_space_tb;
    env->observations[i++] = env->request.duration / (float)env->max_job_duration;
    env->observations[i++] = env->request.negotiations / (float)env->request_timeout;
    env->observations[i++] = env->request.price / job_price(env, env->request);
    env->observations[i++] = env->prev_reward;

    /*
    for (int j=0; j<14; j++) {
        if (env->observations[j] > 1.0f || env->observations[j] < -1.0f) {
            printf("ERROR: observation %d out of range: %f\n", j, env->observations[j]);
            exit(1);
        }
    }
    */
}

void c_reset(CoopHive* env) {
    env->tick = 0;
    for (int i=0; i<NODE_TYPES; i++) {
        env->nodes[i].total = rand()%env->max_nodes + 1;
        env->nodes[i].free = env->nodes[i].total;
    }
    env->space_tb = rand()%(int)env->max_space_tb + 1;
    env->free_space_tb = env->space_tb;
    env->energy = 0;
    env->job_revenue = 0;
    env->energy_revenue = 0;
    env->profit = 0;
    env->prev_reward = 0;
    env->energy_expense = 0;
    env->episode_return = 0;
    memset(env->jobs, 0, MAX_JOBS*sizeof(Job));
    env->request = generate_request(env);
    compute_observations(env);
}

int try_accept_job(CoopHive* env) {
    Job job = env->request;
    for (int i=0; i<MAX_JOBS; i++) {
        if (env->jobs[i].active) {
            continue;
        }
        if (job.space_tb > env->free_space_tb) {
            return 1;
        }
        for (int j=0; j<NODE_TYPES; j++) {
            if (job.nodes[j] > env->nodes[j].free) {
                return 1;
            }
        }
        env->jobs[i] = job;
        for (int j=0; j<NODE_TYPES; j++) {
            env->nodes[j].free -= job.nodes[j];
        }
        env->free_space_tb -= job.space_tb;
        return 0;
    }
    return 1;
}


float job_kw(CoopHive* env, Job job) {
    float kw = 0.0f;
    for (int i=0; i<NODE_TYPES; i++) {
        kw += job.nodes[i]*NODE_ENERGY_KW[i];
    }
    float efficiency = 1.0f + randf(-env->job_efficiency_randomization, env->job_efficiency_randomization);
    return efficiency*kw;
}
 
float buyer_response(CoopHive* env, Job request, float offer_price) {
    float rng = 1.0f + randf(-env->buy_price_randomization, env->buy_price_randomization);
    float respone = rng * job_price(env, request);
}

float calculate_price(float demand, float p0, float threshold, float c) {
    float excess = fmaxf(0.0f, demand - threshold);
    return p0 + c*powf(excess, 2.0f); // Quadratic for non-linear spike
}

float kw_price(CoopHive* env, float t) {
    float demand = env->energy_demand_base + (
        env->a1*cosf(2.0f*PI*t/24.0f) + env->b1*sinf(2.0f*PI*t/24.0f) +
        env->a2*cosf(4.0f*PI*t/24.0f) + env->b2*sinf(4.0f*PI*t/24.0f) +
        env->a3*cosf(6.0f*PI*t/24.0f) + env->b3*sinf(6.0f*PI*t/24.0f));
    float excess = fmaxf(0.0f, demand - env->energy_demand_threshold);
    float price_mwh = env->energy_price_base + env->energy_price_sensitivity*powf(excess, 2.0f);
    return 0.001f*price_mwh;
}

void clear_finished_jobs(CoopHive* env) {
    for (int i=0; i<MAX_JOBS; i++) {
        Job job = env->jobs[i];
        if (!job.active) {
            continue;
        }
        if (env->tick < job.start + job.duration) {
            continue;
        }
        for (int j=0; j<NODE_TYPES; j++) {
            env->nodes[j].free += job.nodes[j];
        }
        env->free_space_tb += job.space_tb;
        memset(&env->jobs[i], 0, sizeof(Job));
    }
}

void update_jobs(CoopHive* env) {
    float reward = 0;
    for (int i=0; i<MAX_JOBS; i++) {
        Job job = env->jobs[i];
        if (!job.active) {
            continue;
        }

        float kw = job_kw(env, job);
        if (env->energy > kw) {
            env->energy -= kw;
            kw = 0;
        } else {
            kw -= env->energy;
            env->energy = 0;
        }

        float energy_cost = kw*kw_price(env, env->tick);
        float profit = job.price - energy_cost;

        env->profit += profit;
        env->job_revenue += job.price;
        env->energy_expense += energy_cost;
        reward += profit;
    }
    
    env->rewards[0] += reward;
} 


void c_step(CoopHive* env) {
    env->rewards[0] = 0;
    env->terminals[0] = 0;

    if (env->tick >= env->episode_length) {
        env->log.score += env->profit;
        env->log.profit += env->profit;
        env->log.energy_expense += env->energy_expense;
        env->log.job_revenue += env->job_revenue;
        env->log.energy_revenue += env->energy_revenue;
        env->log.episode_length += env->tick;
        env->log.episode_return += env->episode_return;
        env->log.n++;
        c_reset(env);
        env->terminals[0] = 1;
    }

    // -0.2 -0.15 -0.1 -0.05 0.0f 0.05 0.1 0.15 0.2
    float price_mul = 1.0f + ((float)env->actions[0] - 4.0f)/20.0f;
    float base_price = job_price(env, env->request);
    float offer_price = price_mul * base_price;
    if (job_is_valid(env->request) && env->request.negotiations < env->request_timeout) {
        float response_price = buyer_response(env, env->request, offer_price);
        env->request.price = response_price;
        if (response_price <= offer_price) {
            env->request.price = offer_price;
            try_accept_job(env);
        } else {
            env->request.negotiations++;
            compute_observations(env);
            return;
        }
    }

    env->tick++;
    clear_finished_jobs(env);

    env->energy += env->energy_gen;
    if (env->energy > env->energy_storage) {
        float diff = env->energy - env->energy_storage;
        env->energy = env->energy_storage;
        float profit = diff*kw_price(env, env->tick);
        env->rewards[0] += profit;
        env->energy_revenue += profit;
        env->profit += profit;
    }

    update_jobs(env);


    // Sell energy
    if (env->actions[1] > 0) {
        float amt = 0.5f * env->energy;
        float profit = amt*kw_price(env, env->tick);
        env->energy_revenue += profit;
        env->profit += profit;
        env->energy -= amt;
        env->rewards[0] += profit;
    }

    // Scale and clip rewards
    float reward = env->rewards[0];
    reward *= env->reward_scale;
    assert(reward >= -1.0f);
    assert(reward <= 1.0f);
    env->rewards[0] = reward;

    env->episode_return += reward;
    env->request = generate_request(env);
    env->prev_reward = reward;

    compute_observations(env);
}

const Color PUFF_RED = (Color){187, 0, 0, 255};
const Color PUFF_CYAN = (Color){0, 187, 187, 255};
const Color PUFF_WHITE = (Color){241, 241, 241, 241};
const Color PUFF_BACKGROUND = (Color){6, 24, 24, 255};

void c_render(CoopHive* env) {
    if (!IsWindowReady()) {
        InitWindow(1080, 720, "PufferLib CoopHive");
        SetTargetFPS(5);
    }

    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }

    BeginDrawing();
    ClearBackground(PUFF_BACKGROUND);
    EndDrawing();
}

void c_close(CoopHive* env) {
    if (IsWindowReady()) {
        CloseWindow();
    }
}
