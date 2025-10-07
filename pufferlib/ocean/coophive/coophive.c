#include "coophive.h"

int main() {
    CoopHive env = {0};
    env.observations = (float*)calloc(11, sizeof(float));
    env.actions = (float*)calloc(2, sizeof(float));
    env.rewards = (float*)calloc(1, sizeof(float));
    env.terminals = (unsigned char*)calloc(1, sizeof(unsigned char));

    c_reset(&env);
    //c_render(&env);
    /*
    while (!WindowShouldClose()) {
        env.actions[0] = 2.0f*((float)rand()/(float)(RAND_MAX)) - 1.0f;
        env.actions[1] = 2.0f*((float)rand()/(float)(RAND_MAX)) - 1.0f;
        c_step(&env);
        //c_render(&env);
    }
    */
    for (int i=0; i<10000000; i++) {
        env.actions[0] = 2.0f*((float)rand()/(float)(RAND_MAX)) - 1.0f;
        env.actions[1] = 2.0f*((float)rand()/(float)(RAND_MAX)) - 1.0f;
        c_step(&env);
        if (env.terminals[0]) {
            c_reset(&env);
        }
    }
 
    free(env.observations);
    free(env.actions);
    free(env.rewards);
    free(env.terminals);
    c_close(&env);
}

