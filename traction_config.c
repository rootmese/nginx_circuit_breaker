#include "traction_config.h"

void *
traction_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_traction_loc_conf_t *conf;

    conf = ngx_pcalloc(
        cf->pool,
        sizeof(ngx_http_traction_loc_conf_t));

    if (conf == NULL)
        return NULL;

    conf->enabled = NGX_CONF_UNSET;

    conf->bucket_count = 60;

    conf->warning_threshold = 80;
    conf->critical_threshold = 50;
    conf->emergency_threshold = 20;

    return conf;
}