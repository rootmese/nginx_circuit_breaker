#include "traction_score.h"
#include "traction_shared_memory.h"

double
traction_calculate_score(void)
{
    uint64_t requests = 0;
    uint64_t errors = 0;

    ngx_uint_t i;

    for (i = 0; i < TRACTION_BUCKETS; i++)
    {
        requests += g_traction->buckets[i].requests;
        errors += g_traction->buckets[i].errors;
    }

    if (requests == 0)
        return 100.0;

    return
        100.0 -
        (((double)errors /
          (double)requests) * 100.0);
}