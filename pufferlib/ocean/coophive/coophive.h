#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "raylib.h"

#define EPISODE_LENGTH 1000
#define MAX_JOBS 100
#define MAX_JOB_DURATION 100

#define ENERGY_GEN 10
#define ENERGY_STORAGE 100

#define MAX_NODES 100
#define MAX_SPACE_TB 100

#define BUY_PRICE_RANDOMIZATION 0.2
#define JOB_EFFICIENCY_RANDOMIZATION 0.2

#define REWARD_SCALE 0.0001

#define SPACE_TB_PRICE 0.03f

float randf(float min, float max) {
    return min + ((float)rand()/(float)(RAND_MAX))*(max-min);
}

typedef struct {
    int price;
    int energy; } NodeSpec;
#define NODE_TYPES 2
const int A100 = 0;
const int H100 = 1;

#define A100_NODE_PRICE 5.31f
#define A100_NODE_ENERGY_KW 6.5f

#define H100_NODE_PRICE 15.92f
#define H100_NODE_ENERGY_KW 10.0f

const float NODE_PRICES[] = {A100_NODE_PRICE, H100_NODE_PRICE};
const float NODE_ENERGY_KW[] = {A100_NODE_ENERGY_KW, H100_NODE_ENERGY_KW};

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
    float energy_gen;
    float energy_storage;
    float job_revenue;
    float energy_revenue;
    float profit;
    float prev_reward;
    float energy_expense;
    float episode_return;
    Job request;
    Job jobs[MAX_JOBS];
} CoopHive;

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
        .space_tb = rand()%env->free_space_tb + 1,
        .start = env->tick,
        .duration = rand()%MAX_JOB_DURATION + 1,
        .active = true
    };
    for (int i=0; i<NODE_TYPES; i++) {
        job.nodes[i] = rand()%env->nodes[i].free + 1;
    }
    return job;
}

bool job_is_valid(Job job) {
    return job.nodes > 0 && job.space_tb > 0;
}

void compute_observations(CoopHive* env) {
    int i = 0;
    for (int j=0; j<NODE_TYPES; j++) {
        env->observations[i++] = env->nodes[j].total / (float)MAX_NODES;
        env->observations[i++] = env->nodes[j].free / (float)MAX_NODES;
    }
    env->observations[i++] = env->space_tb / (float)MAX_SPACE_TB;
    env->observations[i++] = env->free_space_tb / (float)MAX_SPACE_TB;
    env->observations[i++] = env->energy / (float)ENERGY_STORAGE;
    env->observations[i++] = env->energy_gen / (float)ENERGY_GEN;
    env->observations[i++] = env->energy_storage / (float)ENERGY_STORAGE;
    for (int j=0; j<NODE_TYPES; j++) {
        env->observations[i++] = env->request.nodes[j] / (float)MAX_NODES;
    }
    env->observations[i++] = env->request.space_tb / (float)MAX_SPACE_TB;
    env->observations[i++] = env->request.duration / (float)MAX_JOB_DURATION;
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
        env->nodes[i].total = rand()%MAX_NODES + 1;
        env->nodes[i].free = env->nodes[i].total;
    }
    env->space_tb = rand()%MAX_SPACE_TB + 1;
    env->free_space_tb = env->space_tb;
    env->energy = 0;
    env->energy_gen = ENERGY_GEN;
    env->energy_storage = ENERGY_STORAGE;
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
        env->jobs[i] = job;
        for (int j=0; j<NODE_TYPES; j++) {
            env->nodes[j].free -= job.nodes[j];
        }
        env->free_space_tb -= job.space_tb;
        return 0;
    }
    return 1;
}

float job_price(Job job) {
    float price = SPACE_TB_PRICE*job.space_tb;
    for (int i=0; i<NODE_TYPES; i++) {
        price += NODE_PRICES[i]*job.nodes[i];
    }
    return price;
}

float job_kw(Job job) {
    float kw = 0.0f;
    for (int i=0; i<NODE_TYPES; i++) {
        kw += job.nodes[i]*NODE_ENERGY_KW[i];
    }
    float efficiency = 1.0f + randf(-JOB_EFFICIENCY_RANDOMIZATION, JOB_EFFICIENCY_RANDOMIZATION);
    return efficiency*kw;
}
 
bool buyer_accepts(Job request, float offer_price) {
    float rng = 1.0f + randf(-BUY_PRICE_RANDOMIZATION, BUY_PRICE_RANDOMIZATION);
    return offer_price <= rng * job_price(request);
}

float kw_price(float t) {
    return 0.15 + 0.05 * sin(2 * M_PI * t / 24);
}

float avg_kw_price(float t, float T) {
    return T * (0.15 + 0.05 * sin(2 * M_PI * t / 24));
}

void clear_finished_jobs(CoopHive* env) {
    for (int i=0; i<MAX_JOBS; i++) {
        Job job = env->jobs[i];
        if (!job.active) {
            continue;
        }
        if (job.start + job.duration < env->tick) {
            continue;
        }
        for (int j=0; j<NODE_TYPES; j++) {
            env->nodes[j].free += job.nodes[j];
        }
        env->free_space_tb += job.space_tb;
        memset(&env->jobs[i], 0, sizeof(Job));
        job.active = false;
    }
}

void update_jobs(CoopHive* env) {
    float reward = 0;
    for (int i=0; i<MAX_JOBS; i++) {
        Job job = env->jobs[i];
        if (!job.active) {
            continue;
        }

        float kw = job_kw(env->request);
        if (env->energy > kw) {
            env->energy -= kw;
            kw = 0;
        } else {
            kw -= env->energy;
            env->energy = 0;
        }

        float energy_cost = kw*kw_price(env->tick);
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

    if (env->tick >= EPISODE_LENGTH) {
        env->log.score += env->profit;
        env->log.profit += env->profit;
        env->log.energy_expense += env->energy_expense;
        env->log.job_revenue += env->job_revenue;
        env->log.energy_revenue += env->energy_revenue;
        env->log.episode_length += env->tick;
        env->log.episode_return += env->episode_return;
        env->log.n++;
        c_reset(env);
    }
    env->tick++;

    clear_finished_jobs(env);

    env->energy += ENERGY_GEN;
    if (env->energy > ENERGY_STORAGE) {
        float diff = env->energy - ENERGY_STORAGE;
        env->energy = ENERGY_STORAGE;
        float profit = diff*kw_price(env->tick);
        env->rewards[0] += REWARD_SCALE * profit;
        env->energy_revenue += profit;
        env->profit += profit;
    }

    update_jobs(env);

    float base_price = job_price(env->request);

    // -0.2 -0.15 -0.1 -0.05 0.0f 0.05 0.1 0.15 0.2
    float price_mul = 1.0f + ((float)env->actions[0] - 4.0f)/20.0f;
    float offer_price = price_mul * base_price;
    if (offer_price > 0 && job_is_valid(env->request) && buyer_accepts(env->request, offer_price)) {
        env->request.price = offer_price;
        try_accept_job(env);
    }

    // Sell energy
    if (env->actions[1] > 0) {
        float amt = 0.5f * env->energy;
        float profit = amt*kw_price(env->tick);
        env->energy_revenue += profit;
        env->profit += profit;
        env->energy -= amt;
    }

    // Scale and clip rewards
    float reward = env->rewards[0];
    reward *= REWARD_SCALE;
    if (reward > 1.0f) {
        reward = 1.0f;
    }
    if (reward < -1.0f) {
        reward = -1.0f;
    }
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
