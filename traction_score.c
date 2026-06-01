#include "traction_score.h"

traction_stats_t
traction_calculate_stats(traction_zone_shm_t *shm)
{
    traction_stats_t  stats;
    ngx_uint_t        i;
    time_t            now;

    stats.score = 100.0;
    stats.requests = 0;
    stats.errors = 0;

    if (shm == NULL || shm->window == 0) {
        return stats;
    }

    now = ngx_time();

    for (i = 0; i < shm->window; i++) {
        traction_bucket_t  *b;

        b = &shm->buckets[i];

        if (b->epoch + shm->window <= (ngx_uint_t) now) {
            continue;
        }

        stats.requests += (uint64_t) ngx_atomic_fetch_add(&b->requests, 0);
        stats.errors += (uint64_t) ngx_atomic_fetch_add(&b->errors, 0);
    }

    if (stats.requests == 0) {
        stats.score = 100.0;
        return stats;
    }

    stats.score = 100.0
                  - (((double) stats.errors / (double) stats.requests) * 100.0);

    return stats;
}
