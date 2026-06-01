#include "traction_metrics.h"

static traction_bucket_t *
traction_current_bucket(traction_zone_shm_t *shm, time_t now)
{
    ngx_uint_t           idx;
    ngx_uint_t           sec;
    traction_bucket_t   *b;

    sec = (ngx_uint_t) now;
    idx = sec % shm->window;
    b = &shm->buckets[idx];

    if (b->epoch != sec) {
        b->epoch = sec;
        b->requests = 0;
        b->errors = 0;
    }

    return b;
}

void
traction_record_request(traction_zone_shm_t *shm)
{
    traction_bucket_t  *b;

    if (shm == NULL || shm->window == 0) {
        return;
    }

    b = traction_current_bucket(shm, ngx_time());
    (void) ngx_atomic_fetch_add(&b->requests, 1);
}

void
traction_record_error(traction_zone_shm_t *shm)
{
    traction_bucket_t  *b;

    if (shm == NULL || shm->window == 0) {
        return;
    }

    b = traction_current_bucket(shm, ngx_time());
    (void) ngx_atomic_fetch_add(&b->errors, 1);
}
