#include "arkhai.h"

int main() {
    Arkhai env = create_default_env();
    env.side = BUYER;
    //Arkhai env = create_energy_producer();
    //Arkhai env = create_storage_center();
    //Arkhai env = create_premium_hpc();
    init(&env);
    env.observations = (float*)calloc(NUM_OBS, sizeof(float));
    env.actions = (int*)calloc(NUM_ACT, sizeof(int));
    env.rewards = (float*)calloc(1, sizeof(float));
    env.terminals = (unsigned char*)calloc(1, sizeof(unsigned char));

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
