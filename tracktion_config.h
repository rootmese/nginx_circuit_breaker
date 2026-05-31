#ifndef TRACTION_CONFIG_H
#define TRACTION_CONFIG_H

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct
{
    ngx_flag_t enabled;

    ngx_uint_t bucket_count;

    ngx_uint_t emergency_threshold;

    ngx_uint_t critical_threshold;

    ngx_uint_t warning_threshold;

} ngx_http_traction_loc_conf_t;

void *traction_create_loc_conf(ngx_conf_t *cf);

#endif