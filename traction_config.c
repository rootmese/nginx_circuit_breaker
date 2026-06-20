#include "traction_config.h"
#include "traction_shared_memory.h"
#include "traction_warning.h"

extern ngx_module_t  ngx_http_traction_control_module;

void *
traction_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_traction_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_traction_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&conf->zones, cf->pool, 4,
                       sizeof(ngx_http_traction_zone_t))
        != NGX_OK)
    {
        return NULL;
    }

    return conf;
}

void *
traction_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_traction_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_traction_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enabled = NGX_CONF_UNSET;
    conf->status_enabled = NGX_CONF_UNSET;
    conf->log_enabled = NGX_CONF_UNSET;
    conf->latency_threshold = NGX_CONF_UNSET_MSEC;
    conf->warning_threshold = NGX_CONF_UNSET_UINT;
    conf->critical_threshold = NGX_CONF_UNSET_UINT;
    conf->emergency_threshold = NGX_CONF_UNSET_UINT;
    conf->warning_action = NGX_CONF_UNSET_UINT;
    conf->warning_reject_rate = NGX_CONF_UNSET_UINT;

    return conf;
}

ngx_http_traction_zone_t *
traction_zone_find(ngx_conf_t *cf, ngx_str_t *name)
{
    ngx_http_traction_main_conf_t  *tmcf;
    ngx_http_traction_zone_t       *zones;
    ngx_uint_t                      i;

    tmcf = ngx_http_conf_get_module_main_conf(cf,
                                              ngx_http_traction_control_module);
    if (tmcf == NULL) {
        return NULL;
    }

    zones = tmcf->zones.elts;

    for (i = 0; i < tmcf->zones.nelts; i++) {
        if (zones[i].name.len == name->len
            && ngx_strncmp(zones[i].name.data, name->data, name->len) == 0)
        {
            return &zones[i];
        }
    }

    return NULL;
}

char *
traction_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_traction_loc_conf_t  *prev = parent;
    ngx_http_traction_loc_conf_t  *conf = child;

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
    ngx_conf_merge_value(conf->status_enabled, prev->status_enabled, 0);
    ngx_conf_merge_value(conf->log_enabled, prev->log_enabled, 0);
    ngx_conf_merge_msec_value(conf->latency_threshold, prev->latency_threshold, 0);
    ngx_conf_merge_uint_value(conf->warning_threshold, prev->warning_threshold,
                              80);
    ngx_conf_merge_uint_value(conf->critical_threshold, prev->critical_threshold,
                              50);
    ngx_conf_merge_uint_value(conf->emergency_threshold, prev->emergency_threshold,
                              20);
    ngx_conf_merge_uint_value(conf->warning_action, prev->warning_action,
                              TRACTION_WARNING_ACTION_HEADERS);
    ngx_conf_merge_uint_value(conf->warning_reject_rate, prev->warning_reject_rate,
                              TRACTION_WARNING_REJECT_DEFAULT);

    if (conf->warning_action == TRACTION_WARNING_ACTION_RATE_LIMIT
        && (conf->warning_reject_rate < 1 || conf->warning_reject_rate > 99))
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "traction_warning_action rate_limit requires "
                           "reject rate between 1 and 99");
        return NGX_CONF_ERROR;
    }

    if (conf->zone == NULL) {
        conf->zone = prev->zone;
    }

    if (conf->status_zone == NULL) {
        conf->status_zone = prev->status_zone;
    }

    if (conf->warning_threshold > 100 || conf->critical_threshold > 100
        || conf->emergency_threshold > 100)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "traction thresholds must be between 0 and 100");
        return NGX_CONF_ERROR;
    }

    if (conf->emergency_threshold >= conf->critical_threshold
        || conf->critical_threshold >= conf->warning_threshold)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "traction thresholds must satisfy "
                           "emergency < critical < warning");
        return NGX_CONF_ERROR;
    }

    if (conf->enabled && conf->zone == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "traction_control is enabled but no "
                           "traction zone is configured");
        return NGX_CONF_ERROR;
    }

    if (conf->status_enabled && conf->status_zone == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "traction_status requires a zone");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
