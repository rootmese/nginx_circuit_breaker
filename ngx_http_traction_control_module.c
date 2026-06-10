#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "traction_config.h"
#include "traction_shared_memory.h"
#include "traction_warning.h"

static ngx_int_t  traction_init(ngx_conf_t *cf);
static ngx_int_t  traction_init_process(ngx_cycle_t *cycle);
static void       traction_exit_process(ngx_cycle_t *cycle);

static char *ngx_http_traction_control_cmd(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_traction_zone_cmd(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_traction_status_cmd(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_traction_warning_action_cmd(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_traction_zone_slot(ngx_conf_t *cf, ngx_str_t *value,
    ngx_http_traction_zone_t **zone_out);

extern ngx_int_t  traction_handler(ngx_http_request_t *r);
extern ngx_int_t  traction_log_handler(ngx_http_request_t *r);
extern ngx_int_t  traction_header_filter_init(ngx_conf_t *cf);
extern ngx_int_t  traction_status_handler(ngx_http_request_t *r);

static ngx_command_t  traction_commands[] = {

    { ngx_string("traction_zone"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE23,
      ngx_http_traction_zone_cmd,
      0,
      0,
      NULL },

    { ngx_string("traction_control"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_traction_control_cmd,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("traction_status"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_traction_status_cmd,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("traction_warning_threshold"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_traction_loc_conf_t, warning_threshold),
      NULL },

    { ngx_string("traction_critical_threshold"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_traction_loc_conf_t, critical_threshold),
      NULL },

    { ngx_string("traction_emergency_threshold"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_traction_loc_conf_t, emergency_threshold),
      NULL },

    { ngx_string("traction_warning_action"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_traction_warning_action_cmd,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};

static ngx_http_module_t  traction_module_ctx = {
    NULL,                          /* preconfiguration */
    traction_init,                 /* postconfiguration */
    traction_create_main_conf,     /* create_main_conf */
    NULL,                          /* init_main_conf */
    NULL,                          /* create_srv_conf */
    NULL,                          /* merge_srv_conf */
    traction_create_loc_conf,      /* create_loc_conf */
    traction_merge_loc_conf        /* merge_loc_conf */
};

ngx_module_t ngx_http_traction_control_module = {
    NGX_MODULE_V1,
    &traction_module_ctx,
    traction_commands,
    NGX_HTTP_MODULE,

    NULL,                   /* init_master */
    NULL,                   /* init_module */
    traction_init_process,  /* init_process */
    NULL,                   /* init_thread */
    NULL,                   /* exit_thread */
    traction_exit_process,  /* exit_process */
    NULL,                   /* exit_master */

    NGX_MODULE_V1_PADDING
};
static ngx_int_t
ngx_http_traction_zone_slot(ngx_conf_t *cf, ngx_str_t *value,
    ngx_http_traction_zone_t **zone_out)
{
    ngx_str_t  name;

    if (value->len < 6 || ngx_strncmp(value->data, (u_char *) "zone=",
                                      sizeof("zone=") - 1) != 0)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid zone \"%V\", expected zone=name",
                           value);
        return NGX_ERROR;
    }

    name.len = value->len - (sizeof("zone=") - 1);
    name.data = value->data + (sizeof("zone=") - 1);

    *zone_out = traction_zone_find(cf, &name);
    if (*zone_out == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "unknown traction zone \"%V\"", &name);
        return NGX_ERROR;
    }

    return NGX_OK;
}

static char *
ngx_http_traction_zone_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_traction_main_conf_t  *tmcf;
    ngx_http_traction_zone_t       *zone;
    ngx_str_t                      *value;
    ssize_t                         size;
    ngx_uint_t                      i;
    ngx_uint_t                      window;

    tmcf = ngx_http_conf_get_module_main_conf(cf,
                                              ngx_http_traction_control_module);

    value = cf->args->elts;

    if (cf->args->nelts < 3) {
        return "invalid number of arguments";
    }

    if (traction_zone_find(cf, &value[1]) != NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "duplicate traction zone \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    size = ngx_parse_size(&value[2]);
    if (size == NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid traction zone size \"%V\"", &value[2]);
        return NGX_CONF_ERROR;
    }

    window = TRACTION_WINDOW_DEFAULT;

    for (i = 3; i < cf->args->nelts; i++) {
        if (value[i].len > sizeof("window=") - 1
            && ngx_strncmp(value[i].data, (u_char *) "window=",
                           sizeof("window=") - 1) == 0)
        {
            ngx_int_t  n;

            n = ngx_atoi(value[i].data + (sizeof("window=") - 1),
                         value[i].len - (sizeof("window=") - 1));
            if (n == NGX_ERROR || n < 1 || n > TRACTION_WINDOW_MAX) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid traction window, "
                                   "must be between 1 and %ui",
                                   (ngx_uint_t) TRACTION_WINDOW_MAX);
                return NGX_CONF_ERROR;
            }

            window = (ngx_uint_t) n;

        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "unknown traction_zone parameter \"%V\"",
                               &value[i]);
            return NGX_CONF_ERROR;
        }
    }

    if (size == NGX_ERROR || size < (ssize_t) traction_zone_shm_size(window)) {
        size = (ssize_t) traction_zone_shm_size(window);
    }

    zone = ngx_array_push(&tmcf->zones);
    if (zone == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(zone, sizeof(ngx_http_traction_zone_t));
    zone->name = value[1];
    zone->window = window;

    if (traction_zone_register(cf, zone, (size_t) size) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_traction_control_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_traction_loc_conf_t  *tlcf = conf;
    ngx_str_t                     *value;

    value = cf->args->elts;

    if (cf->args->nelts == 2) {
        if (ngx_strcmp(value[1].data, "off") == 0) {
            tlcf->enabled = 0;
            return NGX_CONF_OK;
        }

        if (ngx_strcmp(value[1].data, "on") == 0) {
            tlcf->enabled = 1;
            return NGX_CONF_OK;
        }
    }

    if (cf->args->nelts == 2
        && ngx_http_traction_zone_slot(cf, &value[1], &tlcf->zone) == NGX_OK)
    {
        tlcf->enabled = 1;
        return NGX_CONF_OK;
    }

    return "invalid value, use \"on\", \"off\" or \"zone=name\"";
}

static char *
ngx_http_traction_status_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_traction_loc_conf_t   *tlcf = conf;
    ngx_http_core_loc_conf_t       *clcf;
    ngx_str_t                      *value;

    value = cf->args->elts;

    if (ngx_http_traction_zone_slot(cf, &value[1], &tlcf->status_zone) != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    tlcf->status_enabled = 1;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    if (clcf == NULL) {
        return NGX_CONF_ERROR;
    }

    clcf->handler = traction_status_handler;

    return NGX_CONF_OK;
}

static char *
ngx_http_traction_warning_action_cmd(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    ngx_http_traction_loc_conf_t  *tlcf = conf;
    ngx_str_t                     *value;
    u_char                        *p;
    size_t                         len;
    ngx_int_t                      rate;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "headers") == 0) {
        tlcf->warning_action = TRACTION_WARNING_ACTION_HEADERS;
        return NGX_CONF_OK;
    }

    if (ngx_strcmp(value[1].data, "off") == 0) {
        tlcf->warning_action = TRACTION_WARNING_ACTION_OFF;
        return NGX_CONF_OK;
    }

    len = sizeof("rate_limit=") - 1;

    if (value[1].len <= len
        || ngx_strncmp(value[1].data, (u_char *) "rate_limit=", len) != 0)
    {
        return "invalid value, use \"headers\", \"off\" or \"rate_limit=N%\"";
    }

    p = value[1].data + len;

    if (value[1].data[value[1].len - 1] == '%') {
        rate = ngx_atoi(p, value[1].len - len - 1);
    } else {
        rate = ngx_atoi(p, value[1].len - len);
    }

    if (rate == NGX_ERROR || rate < 1 || rate > 99) {
        return "rate_limit reject percentage must be between 1 and 99";
    }

    tlcf->warning_action = TRACTION_WARNING_ACTION_RATE_LIMIT;
    tlcf->warning_reject_rate = (ngx_uint_t) rate;

    return NGX_CONF_OK;
}

static ngx_int_t
traction_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    if (cmcf == NULL) {
        return NGX_ERROR;
    }

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = traction_handler;

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = traction_log_handler;

    if (traction_header_filter_init(cf) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static ngx_int_t
traction_init_process(ngx_cycle_t *cycle)
{
    return traction_zones_setup(cycle);
}

static void
traction_exit_process(ngx_cycle_t *cycle)
{
    ngx_log_error(NGX_LOG_INFO, cycle->log, 0,
                  "traction: exiting process %P", ngx_pid);
}
