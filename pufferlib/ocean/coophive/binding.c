#include "coophive.h"

#define Env CoopHive 
#include "../env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs) {
    return 0;
}

static int my_log(PyObject* dict, Log* log) {
    assign_to_dict(dict, "score", log->score);
    assign_to_dict(dict, "profit", log->profit);
    assign_to_dict(dict, "job_revenue", log->job_revenue);
    assign_to_dict(dict, "energy_revenue", log->energy_revenue);
    assign_to_dict(dict, "energy_expense", log->energy_expense);
    return 0;
}
