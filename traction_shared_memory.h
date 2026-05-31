#ifndef TRACTION_SHARED_MEMORY_H
#define TRACTION_SHARED_MEMORY_H

#include <ngx_config.h>
#include <ngx_core.h>

#define TRACTION_BUCKETS 60

typedef struct
{
    ngx_atomic_t requests;
    ngx_atomic_t errors;

} traction_bucket_t;

typedef struct
{
    traction_bucket_t buckets[TRACTION_BUCKETS];

} traction_shared_t;

extern traction_shared_t *g_traction;

ngx_int_t traction_init_shm(ngx_conf_t *cf);

#endif