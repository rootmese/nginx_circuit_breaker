// traction_metrics.c - Atualizado
#include "traction_metrics.h"
#include "traction_shared_memory.h"

void traction_record_request(void)
{
    ngx_uint_t bucket;
    traction_shared_t *shm = traction_shm;
    
    if (shm == NULL) {
        // Fallback: se não tem memória compartilhada, não registra
        return;
    }
    
    bucket = ngx_time() % shm->bucket_count;
    
    ngx_atomic_fetch_add(&shm->buckets[bucket].requests, 1);
}

void traction_record_error(void)
{
    ngx_uint_t bucket;
    traction_shared_t *shm = traction_shm;
    
    if (shm == NULL) {
        return;
    }
    
    bucket = ngx_time() % shm->bucket_count;
    
    ngx_atomic_fetch_add(&shm->buckets[bucket].errors, 1);
}