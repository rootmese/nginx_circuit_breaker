// traction_score.c - Atualizado
#include "traction_score.h"
#include "traction_shared_memory.h"

double traction_calculate_score(void)
{
    uint64_t requests = 0;
    uint64_t errors = 0;
    ngx_uint_t i;
    traction_shared_t *shm = traction_shm;
    
    if (shm == NULL) {
        return 100.0;  // Sem dados, assume saudável
    }
    
    for (i = 0; i < shm->bucket_count; i++) {
        requests += shm->buckets[i].requests;
        errors += shm->buckets[i].errors;
    }
    
    if (requests == 0) {
        return 100.0;
    }
    
    return 100.0 - (((double)errors / (double)requests) * 100.0);
}