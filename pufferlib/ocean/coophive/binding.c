#include "coophive.h"

#define Env CoopHive 
#include "../env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs) {
    return 0;
}

static int my_log(PyObject* dict, Log* log) {
    assign_to_dict(dict, "score", log->score);
    return 0;
}
