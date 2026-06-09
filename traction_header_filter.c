#include <ngx_http.h>
#include "traction_config.h"
#include "traction_score.h"
#include "traction_state.h"
#include "traction_warning.h"

extern ngx_module_t  ngx_http_traction_control_module;

static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;

static ngx_int_t
traction_push_header(ngx_http_request_t *r, u_char *name, size_t name_len,
    u_char *value, size_t value_len)
{
    ngx_table_elt_t  *h;
    u_char           *p;

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    p = ngx_pnalloc(r->pool, name_len + 1);
    if (p == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(p, name, name_len);
    p[name_len] = '\0';

    h->key.len = name_len;
    h->key.data = p;
    h->hash = ngx_hash_key(p, name_len);

    p = ngx_pnalloc(r->pool, value_len + 1);
    if (p == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(p, value, value_len);
    p[value_len] = '\0';

    h->value.len = value_len;
    h->value.data = p;

    return NGX_OK;
}

ngx_int_t
traction_header_filter(ngx_http_request_t *r)
{
    ngx_http_traction_loc_conf_t  *conf;
    traction_stats_t               stats;
    traction_state_e               state;
    u_char                         score_buf[32];
    u_char                         zone_buf[256];
    size_t                         score_len;
    size_t                         zone_len;
    u_char                        *state_name;
    size_t                         state_len;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_traction_control_module);

    if (!conf->enabled || conf->zone == NULL || conf->zone->shm == NULL) {
        return ngx_http_next_header_filter(r);
    }

    stats = traction_calculate_stats(conf->zone->shm);
    state = traction_get_state(conf, stats.score, conf->zone->shm);

    if ((state != TRACTION_STATE_WARNING && state != TRACTION_STATE_RECOVERY)
        || !traction_warning_emit_headers(conf))
    {
        return ngx_http_next_header_filter(r);
    }

    if (state == TRACTION_STATE_RECOVERY) {
        state_name = (u_char *) "recovery";
        state_len = sizeof("recovery") - 1;
    } else {
        state_name = (u_char *) "warning";
        state_len = sizeof("warning") - 1;
    }

    if (traction_push_header(r, (u_char *) "X-Traction-State",
                             sizeof("X-Traction-State") - 1,
                             state_name, state_len) != NGX_OK)
    {
        return NGX_ERROR;
    }

    score_len = ngx_snprintf(score_buf, sizeof(score_buf), "%.2f",
                             stats.score) - score_buf;

    if (traction_push_header(r, (u_char *) "X-Traction-Score",
                             sizeof("X-Traction-Score") - 1,
                             score_buf, score_len) != NGX_OK)
    {
        return NGX_ERROR;
    }

    zone_len = conf->zone->name.len;
    if (zone_len >= sizeof(zone_buf)) {
        zone_len = sizeof(zone_buf) - 1;
    }

    ngx_memcpy(zone_buf, conf->zone->name.data, zone_len);

    if (traction_push_header(r, (u_char *) "X-Traction-Zone",
                             sizeof("X-Traction-Zone") - 1,
                             zone_buf, zone_len) != NGX_OK)
    {
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                  "traction: zone \"%V\" in warning state (score=%.2f)",
                  &conf->zone->name, stats.score);

    return ngx_http_next_header_filter(r);
}

ngx_int_t
traction_header_filter_init(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = traction_header_filter;

    return NGX_OK;
}