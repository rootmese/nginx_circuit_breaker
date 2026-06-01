#include <ngx_http.h>
#include "traction_decision.h"
#include "traction_state.h"
#include "traction_warning.h"

ngx_int_t
traction_decide(ngx_http_request_t *r, ngx_http_traction_loc_conf_t *conf,
                double score)
{
    traction_state_e  state;

    state = traction_get_state(conf, score);

    if (state == TRACTION_STATE_EMERGENCY) {
        if (conf->zone != NULL) {
            r->headers_out.retry_after = ngx_time() + conf->zone->window;
        }

        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    if (state == TRACTION_STATE_CRITICAL) {
        r->headers_out.retry_after = ngx_time() + 1;
        return NGX_HTTP_TOO_MANY_REQUESTS;
    }

    if (state == TRACTION_STATE_WARNING
        && conf->zone != NULL
        && conf->zone->shm != NULL
        && traction_warning_should_shed(conf, conf->zone->shm))
    {
        r->headers_out.retry_after = ngx_time() + 1;

        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "traction: zone \"%V\" warning rate_limit shed "
                      "(score=%.2f, reject=%ui%%)",
                      &conf->zone->name, score, conf->warning_reject_rate);

        return NGX_HTTP_TOO_MANY_REQUESTS;
    }

    return NGX_DECLINED;
}
