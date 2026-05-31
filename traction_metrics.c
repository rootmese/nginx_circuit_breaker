#include "traction_metrics.h"
#include "traction_shared_memory.h"

void
traction_record_request(void)
{
    ngx_uint_t bucket;

    bucket = ngx_time() % TRACTION_BUCKETS;

    ngx_atomic_fetch_add(
        &g_traction->buckets[bucket].requests,
        1);
}

void
traction_record_error(void)
{
    ngx_uint_t bucket;

    bucket = ngx_time() % TRACTION_BUCKETS;

    ngx_atomic_fetch_add(
        &g_traction->buckets[bucket].errors,
        1);
}