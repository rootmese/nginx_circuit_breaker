#ifndef TRACTION_SHARED_MEMORY_H
#define TRACTION_SHARED_MEMORY_H

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define TRACTION_BUCKETS_DEFAULT 60
#define TRACTION_SHM_NAME "traction_shm"

typedef struct {
    ngx_atomic_t requests;
    ngx_atomic_t errors;
} traction_bucket_t;

typedef struct {
    traction_bucket_t buckets[TRACTION_BUCKETS_DEFAULT];
    ngx_uint_t last_rotate;      // Último timestamp de rotação
    ngx_uint_t bucket_count;     // Número real de buckets
} traction_shared_t;

// Ponteiro global para memória compartilhada
extern traction_shared_t *traction_shm;

// Inicializa a zona de memória compartilhada
ngx_int_t traction_init_shm(ngx_conf_t *cf);

// Callback chamado quando a zona é alocada/inicializada
void *traction_shm_alloc(ngx_shm_zone_t *shm_zone, void *data);

// Obtém o ponteiro para memória compartilhada no ciclo atual
ngx_int_t traction_shm_init_cycle(ngx_cycle_t *cycle);

#endif