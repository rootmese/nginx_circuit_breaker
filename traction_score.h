#ifndef TRACTION_SCORE_H
#define TRACTION_SCORE_H

#include "traction_shared_memory.h"

typedef struct {
    double    score;
    uint64_t  requests;
    uint64_t  errors;
} traction_stats_t;

traction_stats_t  traction_calculate_stats(traction_zone_shm_t *shm);

#endif
