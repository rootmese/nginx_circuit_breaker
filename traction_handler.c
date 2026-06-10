#include <ngx_http.h>
#include "traction_config.h"
#include "traction_metrics.h"
#include "traction_score.h"
#include "traction_decision.h"

extern ngx_module_t  ngx_http_traction_control_module;

ngx_int_t
traction_handler(ngx_http_request_t *r)
{
    ngx_http_traction_loc_conf_t  *conf;
    traction_stats_t               stats;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_traction_control_module);

    if (!conf->enabled || conf->zone == NULL) {
        return NGX_DECLINED;
    }

    if (conf->zone->shm == NULL) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "traction: zone \"%V\" shared memory unavailable, "
                      "allowing request", &conf->zone->name);
        return NGX_DECLINED;
    }

    stats = traction_calculate_stats(conf->zone->shm);

    return traction_decide(r, conf, stats.score);
}

ngx_int_t
traction_log_handler(ngx_http_request_t *r)
{
    ngx_http_traction_loc_conf_t  *conf;
    ngx_uint_t                     status;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_traction_control_module);

    if (!conf->enabled || conf->zone == NULL || conf->zone->shm == NULL) {
        return NGX_DECLINED;
    }

    status = r->headers_out.status;

    if (status == 0 || r->upstream == NULL) {
        return NGX_DECLINED;
    }

    traction_record_request(conf->zone->shm);

    if (status >= NGX_HTTP_INTERNAL_SERVER_ERROR) {
        traction_record_error(conf->zone->shm);
    }

    return NGX_DECLINED;
}
