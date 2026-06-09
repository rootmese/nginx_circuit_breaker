#ifndef TRACTION_SHARED_MEMORY_H
#define TRACTION_SHARED_MEMORY_H

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define TRACTION_WINDOW_MAX      3600
#define TRACTION_WINDOW_DEFAULT  60

typedef struct {
    ngx_atomic_t  requests;
    ngx_atomic_t  errors;
    ngx_atomic_t  epoch;
} traction_bucket_t;

typedef struct {
    ngx_uint_t         window;
    ngx_atomic_t       shed_counter;
    ngx_atomic_t       last_state;
    traction_bucket_t  buckets[1];
} traction_zone_shm_t;

typedef struct {
    ngx_uint_t  window;
} traction_zone_conf_t;

/*
 * Forward declaration para evitar dependência circular
 * com traction_config.h
 */
struct ngx_http_traction_zone_s;
typedef struct ngx_http_traction_zone_s ngx_http_traction_zone_t;

/*
 * Tamanho necessário da shared memory para uma janela
 * de N buckets.
 */
#define traction_zone_shm_size(window) \
    (offsetof(traction_zone_shm_t, buckets) + \
     ((window) * sizeof(traction_bucket_t)))

ngx_int_t traction_zone_register(
    ngx_conf_t *cf,
    ngx_http_traction_zone_t *zone,
    size_t size);

ngx_int_t traction_zones_setup(ngx_cycle_t *cycle);

#endif /* TRACTION_SHARED_MEMORY_H */