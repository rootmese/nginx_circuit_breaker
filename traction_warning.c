#include "traction_warning.h"

ngx_flag_t
traction_warning_emit_headers(ngx_http_traction_loc_conf_t *conf)
{
    return conf->warning_action == TRACTION_WARNING_ACTION_HEADERS
           || conf->warning_action == TRACTION_WARNING_ACTION_RATE_LIMIT;
}

ngx_flag_t
traction_warning_should_shed(ngx_http_traction_loc_conf_t *conf,
    traction_zone_shm_t *shm)
{
    ngx_uint_t  n;

    if (conf->warning_action != TRACTION_WARNING_ACTION_RATE_LIMIT) {
        return 0;
    }

    if (shm == NULL || conf->warning_reject_rate == 0) {
        return 0;
    }

    n = (ngx_uint_t) ngx_atomic_fetch_add(&shm->shed_counter, 1);

    return (n % 100) < conf->warning_reject_rate;
}

const char *
traction_warning_action_name(ngx_http_traction_loc_conf_t *conf)
{
    switch (conf->warning_action) {
    case TRACTION_WARNING_ACTION_OFF:
        return "off";
    case TRACTION_WARNING_ACTION_RATE_LIMIT:
        return "rate_limit";
    default:
        return "headers";
    }
}
