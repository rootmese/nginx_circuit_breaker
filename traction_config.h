#ifndef TRACTION_CONFIG_H
#define TRACTION_CONFIG_H

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct ngx_http_traction_zone_s {
    ngx_str_t               name;
    ngx_shm_zone_t         *shm_zone;
    struct traction_zone_shm_s  *shm;
    ngx_uint_t              window;
} ngx_http_traction_zone_t;

typedef struct {
    ngx_array_t             zones;
} ngx_http_traction_main_conf_t;

typedef struct {
    ngx_flag_t              enabled;
    ngx_flag_t              status_enabled;
    ngx_http_traction_zone_t  *zone;
    ngx_http_traction_zone_t  *status_zone;
    ngx_uint_t              warning_threshold;
    ngx_uint_t              critical_threshold;
    ngx_uint_t              emergency_threshold;
    ngx_uint_t              warning_action;
    ngx_uint_t              warning_reject_rate;
} ngx_http_traction_loc_conf_t;

void   *traction_create_main_conf(ngx_conf_t *cf);
void   *traction_create_loc_conf(ngx_conf_t *cf);
char   *traction_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

ngx_http_traction_zone_t  *traction_zone_find(ngx_conf_t *cf, ngx_str_t *name);

#endif
