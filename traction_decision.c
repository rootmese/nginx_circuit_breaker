#include <ngx_http.h>
#include "traction_decision.h"
#include "traction_state.h"
#include "traction_warning.h"

static void
traction_log_state_transition(ngx_http_request_t *r,
    ngx_http_traction_loc_conf_t *conf, traction_state_e state, double score)
{
    traction_zone_shm_t  *shm;
    ngx_atomic_t          old_state;

    if (conf == NULL || conf->zone == NULL || conf->zone->shm == NULL) {
        return;
    }

    shm = conf->zone->shm;
    old_state = ngx_atomic_fetch_add(&shm->last_state, 0);

    if ((ngx_uint_t) old_state == (ngx_uint_t) state) {
        return;
    }

    if (ngx_atomic_cmp_set(&shm->last_state, old_state, (ngx_atomic_t) state)) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "traction: zone \"%V\" state transition %s -> %s "
                      "(score=%.2f)",
                      &conf->zone->name,
                      traction_state_name((traction_state_e) old_state),
                      traction_state_name(state),
                      score);
    }
}

static ngx_uint_t
traction_recovery_allow_rate(ngx_http_traction_loc_conf_t *conf,
                             double score)
{
    ngx_uint_t  range;
    double      relative;

    if (conf->critical_threshold <= conf->emergency_threshold) {
        return 50;
    }

    range = conf->critical_threshold - conf->emergency_threshold;
    relative = (score - (double) conf->emergency_threshold) /
               (double) range;

    if (relative < 0.33) {
        return 10;
    }

    if (relative < 0.66) {
        return 20;
    }

    return 50;
}

static ngx_flag_t
traction_recovery_should_shed(ngx_http_traction_loc_conf_t *conf,
    traction_zone_shm_t *shm, double score)
{
    ngx_uint_t  n;
    ngx_uint_t  allow_rate;

    if (shm == NULL) {
        return 0;
    }

    allow_rate = traction_recovery_allow_rate(conf, score);
    n = (ngx_uint_t) ngx_atomic_fetch_add(&shm->shed_counter, 1);

    return (n % 100) >= allow_rate;
}

ngx_int_t
traction_decide(ngx_http_request_t *r, ngx_http_traction_loc_conf_t *conf,
                double score)
{
    traction_state_e  state;
    traction_zone_shm_t *shm;
    ngx_uint_t allow_rate;

    shm = conf->zone != NULL ? conf->zone->shm : NULL;
    state = traction_get_state(conf, score, shm);
    traction_log_state_transition(r, conf, state, score);

    if (state == TRACTION_STATE_EMERGENCY) {
        if (conf->zone != NULL) {
            r->headers_out.retry_after = ngx_time() + conf->zone->window;
        }

        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    if (state == TRACTION_STATE_RECOVERY) {
        if (conf->zone != NULL
            && conf->zone->shm != NULL
            && traction_recovery_should_shed(conf, conf->zone->shm, score))
        {
            allow_rate = traction_recovery_allow_rate(conf, score);
            r->headers_out.retry_after = ngx_time() + 1;

            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                          "traction: zone \"%V\" recovery shed "
                          "(score=%.2f, allow=%ui%%)",
                          &conf->zone->name, score, allow_rate);

            return NGX_HTTP_TOO_MANY_REQUESTS;
        }

        return NGX_DECLINED;
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
