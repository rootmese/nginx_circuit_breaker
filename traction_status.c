#include <ngx_http.h>
#include "traction_config.h"
#include "traction_score.h"
#include "traction_state.h"
#include "traction_warning.h"

extern ngx_module_t  ngx_http_traction_control_module;

ngx_int_t  traction_status_handler(ngx_http_request_t *r);

static ngx_flag_t traction_status_is_local_request(ngx_http_request_t *r);

ngx_int_t
traction_status_handler(ngx_http_request_t *r)
{
    ngx_http_traction_loc_conf_t  *conf;
    traction_stats_t               stats;
    traction_state_e               state;
    double                         error_rate;
    ngx_buf_t                     *b;
    ngx_chain_t                    out;
    u_char                        *start;
    u_char                        *end;
    size_t                         len;
    ngx_http_traction_zone_t      *zone;

    if (r->method != NGX_HTTP_GET && r->method != NGX_HTTP_HEAD) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    conf = ngx_http_get_module_loc_conf(r, ngx_http_traction_control_module);

    if (!conf->status_enabled || conf->status_zone == NULL) {
        return NGX_DECLINED;
    }

    zone = conf->status_zone;

    if (zone->shm == NULL) {
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    if (!traction_status_is_local_request(r)) {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "traction: status endpoint served to non-local client; "
                      "protect this location with allow/deny");
    }

    stats = traction_calculate_stats(zone->shm);
    state = traction_get_state(
    conf,
    stats.score,
    zone->shm);

    if (stats.requests > 0) {
        error_rate = ((double) stats.errors / (double) stats.requests) * 100.0;
    } else {
        error_rate = 0.0;
    }

    len = zone->name.len + 512;

    start = ngx_pnalloc(r->pool, len);
    if (start == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    end = ngx_snprintf(start, len,
        "Traction Status\n"
        "===============\n"
        "zone: %V\n"
        "window: %ui s\n"
        "score: %.2f\n"
        "requests: %uL\n"
        "errors: %uL\n"
        "error_rate: %.2f%%\n"
        "state: %s\n"
        "thresholds: warning=%ui critical=%ui emergency=%ui\n"
        "warning_action: %s\n"
        "warning_reject_rate: %ui%%\n",
        &zone->name,
        zone->window,
        stats.score,
        stats.requests,
        stats.errors,
        error_rate,
        traction_state_name(state),
        conf->warning_threshold,
        conf->critical_threshold,
        conf->emergency_threshold,
        traction_warning_action_name(conf),
        conf->warning_action == TRACTION_WARNING_ACTION_RATE_LIMIT
            ? conf->warning_reject_rate : (ngx_uint_t) 0);

    len = (size_t) (end - start);

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_type_len = sizeof("text/plain") - 1;
    ngx_str_set(&r->headers_out.content_type, "text/plain");
    r->headers_out.content_length_n = len;

    b = ngx_create_temp_buf(r->pool, len);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->last = ngx_cpymem(b->last, start, len);
    b->last_buf = (ngx_uint_t) 1;
    b->last_in_chain = (ngx_uint_t) 1;

    out.buf = b;
    out.next = NULL;

    {
        ngx_int_t rc = ngx_http_send_header(r);
        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
            return rc;
        }
    }

    return ngx_http_output_filter(r, &out);
}

static ngx_flag_t
traction_status_is_local_request(ngx_http_request_t *r)
{
    struct sockaddr  *sa;

    if (r == NULL || r->connection == NULL || r->connection->sockaddr == NULL) {
        return 0;
    }

    sa = r->connection->sockaddr;

    if (sa->sa_family == AF_INET) {
        struct sockaddr_in  *sin = (struct sockaddr_in *) sa;
        return sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK);
    }

#if (NGX_HAVE_INET6)
    if (sa->sa_family == AF_INET6) {
        struct sockaddr_in6  *sin6 = (struct sockaddr_in6 *) sa;
        return IN6_ARE_ADDR_EQUAL(&sin6->sin6_addr, &in6addr_loopback);
    }
#endif

    return 0;
}

