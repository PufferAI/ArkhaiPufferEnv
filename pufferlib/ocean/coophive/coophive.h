#include <stdlib.h>
#include <string.h>
#include "raylib.h"

#define EPISODE_LENGTH 1000
#define NUM_JOBS 100
#define MAX_JOB_DURATION 100

typedef struct {
    float score;
    float n;
} Log;

typedef struct {
    Log log;
    unsigned char* observations;
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
    float profit;
    Job request;
    Job jobs[NUM_JOBS];
} CoopHive;

typedef struct {
    int nodes;
    int space_tb;
    int start;
    int duration;
} Job;

Job generate_request(CoopHive* env) {
    return {
        .nodes = rand() % env->free_nodes,
        .space_tb = rand() % env->free_space_tb,
        .start = env->tick,
        .duration = rand() % MAX_JOB_DURATION
    };
}

void c_reset(CoopHive* env) {
    env->tick = 0;
    env->profit = 0;
    env->energy = 0;
    env->energy_gen = ENERGY_GEN;
    env->request = generate_request(env);
    memset(env->jobs, 0, NUM_JOBS*sizeof(Job));
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

void c_step(CoopHive* env) {
    env->rewards[0] = 0;
    env->terminals[0] = 0;

    if (env->tick >= EPISODE_LENGTH) {
        env->log.score = env->profit;
        env->log.n++;
        c_reset(env);
    }
    env->tick++;

    float offer_price = env->actions[0];
    if (buyer_accepts(env->request, offer_price)) {
        int err = accept_job(env);
        if (!err) {
            env->profit += offer_price;
        }
    } 

    bool sell_energy = env->actions[1] > 0;
    if (sell_energy) {
        // Sell at current market price
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

    DrawText("Go to the red square!", 20, 20, 20, PUFF_WHITE);
    DrawRectangle(540 - 32 + 64*env->goal, 360 - 32, 64, 64, PUFF_RED);
    DrawRectangle(540 - 32 + 64*env->x, 360 - 32, 64, 64, PUFF_CYAN);

    BeginDrawing();
    ClearBackground(PUFF_BACKGROUND);
    EndDrawing();
}

void c_close(CoopHive* env) {
    if (IsWindowReady()) {
        CloseWindow();
    }
}
