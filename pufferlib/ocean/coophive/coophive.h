#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "raylib.h"

#define EPISODE_LENGTH 100
#define NUM_JOBS 100
#define MAX_JOB_DURATION 100
#define ENERGY_GEN 100
#define ENERGY_STORAGE 100
#define MAX_NODES 100
#define MAX_SPACE_TB 100

#define SPACE_TB_PRICE 0.03f
#define NODE_PRICE 5.31f // A100 8x Node price
#define NODE_ENERGY_KW 6.5f
#define BUY_PRICE_RANDOMIZATION 0.1

#define ENERGY_USAGE_RANOMIZATION 0.5

typedef struct {
    float score;
    float revenue;
    float profit;
    float energy_expense;
    float n;
} Log;

typedef struct {
    int nodes;
    int space_tb;
    int start;
    int duration;
} Job;

typedef struct {
    Log log;
    float* observations;
    float * actions;
    float* rewards;
    unsigned char* terminals;
    int tick;
    int nodes;
    int free_nodes;
    int space_tb;
    int free_space_tb;
    float energy;
    float energy_gen;
    float energy_storage;
    float revenue;
    float profit;
    float energy_expense;
    Job request;
    Job jobs[NUM_JOBS];
} CoopHive;

Job generate_request(CoopHive* env) {
    if (env->free_nodes == 0) {
        return (Job){0};
    }
    if (env->free_space_tb == 0) {
        return (Job){0};
    }
    return (Job) {
        .nodes = rand()%env->free_nodes + 1,
        .space_tb = rand()%env->free_space_tb + 1,
        .start = env->tick,
        .duration = rand()%MAX_JOB_DURATION + 1
    };
}

bool job_is_valid(Job job) {
    return job.nodes > 0 && job.space_tb > 0;
}

void c_reset(CoopHive* env) {
    env->tick = 0;
    env->nodes = rand()%MAX_NODES + 1;
    env->free_nodes = env->nodes;
    env->space_tb = rand()%MAX_SPACE_TB + 1;
    env->free_space_tb = env->space_tb;
    env->energy = 0;
    env->energy_gen = ENERGY_GEN;
    env->energy_storage = ENERGY_STORAGE;
    env->revenue = 0;
    env->profit = 0;
    env->energy_expense = 0;
    memset(env->jobs, 0, NUM_JOBS*sizeof(Job));
    env->request = generate_request(env);
}

int accept_job(CoopHive* env) {
    Job job = env->request;
    for (int i=0; i<NUM_JOBS; i++) {
        if (env->jobs[i].nodes != 0) {
            continue;
        }
        env->jobs[i] = job;
        env->free_nodes -= job.nodes;
        env->free_space_tb -= job.space_tb;
        return 0;
    }
    return 1;
}

float randf(float min, float max) {
    return min + ((float)rand()/(float)(RAND_MAX))*(max-min);
}

bool buyer_accepts(Job request, float offer_price) {
    if (offer_price < 0) {
        return false;
    }
    float rng = 1.0f + randf(-BUY_PRICE_RANDOMIZATION, BUY_PRICE_RANDOMIZATION);
    float worth = rng*(
        NODE_PRICE*request.nodes
        + SPACE_TB_PRICE*request.space_tb
    );
    return offer_price <= worth;
}

float energy_price(float t) {
    return 0.15 + 0.05 * sin(2 * M_PI * t / 24);
}

float job_energy_cost(float t, float T) {
    return T * (0.15 + 0.05 * sin(2 * M_PI * t / 24));
}

void compute_observations(CoopHive* env) {
    int i = 0;
    env->observations[i++] = env->nodes / (float)MAX_NODES;
    env->observations[i++] = env->free_nodes / (float)MAX_NODES;
    env->observations[i++] = env->space_tb / (float)MAX_SPACE_TB;
    env->observations[i++] = env->free_space_tb / (float)MAX_SPACE_TB;
    env->observations[i++] = env->energy / (float)ENERGY_STORAGE;
    env->observations[i++] = env->energy_gen / (float)ENERGY_GEN;
    env->observations[i++] = env->energy_storage / (float)ENERGY_STORAGE;
    env->observations[i++] = env->request.nodes / (float)MAX_NODES;
    env->observations[i++] = env->request.space_tb / (float)MAX_SPACE_TB;
    env->observations[i++] = env->request.duration / (float)MAX_JOB_DURATION;
}

void c_step(CoopHive* env) {
    env->rewards[0] = 0;
    env->terminals[0] = 0;

    if (env->tick >= EPISODE_LENGTH) {
        env->log.score += env->profit;
        env->log.profit += env->profit;
        env->log.energy_expense += env->energy_expense;
        env->log.revenue += env->revenue;
        env->log.n++;
        c_reset(env);
    }
    env->tick++;

    for (int i=0; i<NUM_JOBS; i++) {
        Job job = env->jobs[i];
        if (job.start + job.duration >= env->tick) {
            env->free_nodes += job.nodes;
            env->free_space_tb += job.space_tb;
            memset(&env->jobs[i], 0, sizeof(Job));
        }
    }

    float base_price = NODE_PRICE*env->request.nodes + SPACE_TB_PRICE*env->request.space_tb;
    float offer_price = base_price * env->actions[0];
    if (job_is_valid(env->request)
            && buyer_accepts(env->request, offer_price)) {
        int err = accept_job(env);
        if (!err) {
            float energy_cost = NODE_ENERGY_KW*env->request.nodes*job_energy_cost(env->tick, env->request.duration);
            //printf("energy_cost: %f for %d nodes for %d hours\n", energy_cost, env->request.nodes, env->request.duration);
            env->profit += offer_price - energy_cost;
            env->revenue += offer_price;
            env->energy_expense += energy_cost;
        }
    } 

    bool sell_energy = env->actions[1] > 0;
    if (sell_energy) {
    }

    compute_observations(env);
    env->request = generate_request(env);
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
