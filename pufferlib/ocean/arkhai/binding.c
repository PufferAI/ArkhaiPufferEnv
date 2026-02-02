#include "arkhai.h"

#define Env Arkhai 
#include "../env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs) {
    env->ai_sellers = unpack(kwargs, "ai_sellers");
    env->ai_buyers = unpack(kwargs, "ai_buyers");
    env->scripted_sellers = unpack(kwargs, "scripted_sellers");
    env->scripted_buyers = unpack(kwargs, "scripted_buyers");
    env->episode_length = unpack(kwargs, "episode_length");
    env->request_timeout = unpack(kwargs, "request_timeout");
    env->job_nodes[A100] = unpack(kwargs, "job_a100_nodes");
    env->job_nodes[H100] = unpack(kwargs, "job_h100_nodes");
    env->job_nodes[R5090] = unpack(kwargs, "job_r5090_nodes");
    env->job_nodes_dr[A100] = unpack(kwargs, "job_a100_nodes_dr");
    env->job_nodes_dr[H100] = unpack(kwargs, "job_h100_nodes_dr");
    env->job_nodes_dr[R5090] = unpack(kwargs, "job_r5090_nodes_dr");
    env->job_duration = unpack(kwargs, "job_duration");
    env->job_duration_dr = unpack(kwargs, "job_duration_dr");
    env->job_tb_usage = unpack(kwargs, "job_tb_usage");
    env->job_tb_usage_dr = unpack(kwargs, "job_tb_usage_dr");
    env->job_efficiency = unpack(kwargs, "job_efficiency");
    env->job_efficiency_dr = unpack(kwargs, "job_efficiency_dr");
    env->scripted_buy_price = unpack(kwargs, "scripted_buy_price");
    env->scripted_buy_price_dr = unpack(kwargs, "scripted_buy_price_dr");
    env->scripted_sell_price = unpack(kwargs, "scripted_sell_price");
    env->scripted_sell_price_dr = unpack(kwargs, "scripted_sell_price_dr");
    env->reward_scale = unpack(kwargs, "reward_scale");
    env->tb_price = unpack(kwargs, "tb_price");
    env->a100_price = unpack(kwargs, "a100_price");
    env->a100_kw = unpack(kwargs, "a100_kw");
    env->h100_price = unpack(kwargs, "h100_price");
    env->h100_kw = unpack(kwargs, "h100_kw");
    env->r5090_price = unpack(kwargs, "r5090_price");
    env->r5090_kw = unpack(kwargs, "r5090_kw");
    env->energy_demand_base = unpack(kwargs, "energy_demand_base");
    env->kwh_price_base = unpack(kwargs, "kwh_price_base");
    env->kwh_price_sensitivity = unpack(kwargs, "kwh_price_sensitivity");
    env->kwh_demand_threshold = unpack(kwargs, "kwh_demand_threshold");
    env->a1 = unpack(kwargs, "a1");
    env->b1 = unpack(kwargs, "b1");
    env->a2 = unpack(kwargs, "a2");
    env->b2 = unpack(kwargs, "b2");
    env->a3 = unpack(kwargs, "a3");
    env->b3 = unpack(kwargs, "b3");
    env->randomize_offset = unpack(kwargs, "randomize_offset");
    env->preset = unpack(kwargs, "preset");
    ClusterSpec seller_spec = {
        .node_capacity = {
            unpack(kwargs, "cluster_a100_capacity"),
            unpack(kwargs, "cluster_h100_capacity"),
            unpack(kwargs, "cluster_r5090_capacity"),
        },
        .node_capacity_dr = {
            unpack(kwargs, "cluster_a100_capacity_dr"),
            unpack(kwargs, "cluster_h100_capacity_dr"),
            unpack(kwargs, "cluster_r5090_capacity_dr"),
        },
        .tb_capacity = unpack(kwargs, "cluster_tb_capacity"),
        .tb_capacity_dr = unpack(kwargs, "cluster_tb_capacity_dr"),
        .kwh_capacity = unpack(kwargs, "cluster_kwh_capacity"),
        .kwh_capacity_dr = unpack(kwargs, "cluster_kwh_capacity_dr"),
        .kw_generation = unpack(kwargs, "cluster_kw_generation"),
        .kw_generation_dr = unpack(kwargs, "cluster_kw_generation_dr"),
    };
    ClusterSpec buyer_spec = {0};
    init(env, buyer_spec, seller_spec);
    return 0;
}

static int my_log(PyObject* dict, Log* log) {
    assign_to_dict(dict, "score", log->score);
    assign_to_dict(dict, "expense", log->expense);
    assign_to_dict(dict, "profit", log->profit);
    assign_to_dict(dict, "episode_length", log->episode_length);
    assign_to_dict(dict, "episode_return", log->episode_return);
    return 0;
}
