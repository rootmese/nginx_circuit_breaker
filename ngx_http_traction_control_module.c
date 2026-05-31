#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

extern ngx_int_t traction_handler(
    ngx_http_request_t *r);

static ngx_int_t
traction_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt *h;

    ngx_http_core_main_conf_t *cmcf;

    cmcf =
        ngx_http_conf_get_module_main_conf(
            cf,
            ngx_http_core_module);

    h = ngx_array_push(
        &cmcf->phases[
            NGX_HTTP_PREACCESS_PHASE
        ].handlers);

    if (h == NULL)
        return NGX_ERROR;

    *h = traction_handler;

    return NGX_OK;
}

static ngx_http_module_t traction_module_ctx =
{
    NULL,
    traction_init,

    NULL,
    NULL,

    NULL,
    NULL,

    NULL,
    NULL
};

ngx_module_t ngx_http_traction_control_module =
{
    NGX_MODULE_V1,

    &traction_module_ctx,

    NULL,

    NGX_HTTP_MODULE,

    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,

    NGX_MODULE_V1_PADDING
};