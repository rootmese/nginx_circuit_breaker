#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "traction_shared_memory.h"

extern ngx_int_t traction_handler(ngx_http_request_t *r);

// Declaração da função de setup (será chamada via ngx_add_inherited_shm)
static ngx_int_t traction_init_process(ngx_cycle_t *cycle);
static void traction_exit_process(ngx_cycle_t *cycle);

static ngx_int_t
traction_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;
    
    // Inicializa memória compartilhada
    if (traction_init_shm(cf) != NGX_OK) {
        return NGX_ERROR;
    }
    
    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    if (cmcf == NULL) {
        return NGX_ERROR;
    }
    
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    
    *h = traction_handler;
    
    return NGX_OK;
}

// Inicialização do processo (worker)
static ngx_int_t
traction_init_process(ngx_cycle_t *cycle)
{
    traction_shm_setup(cycle);
    
    if (traction_shm == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "traction: failed to setup shared memory");
        return NGX_ERROR;
    }
    
    return NGX_OK;
}

static void
traction_exit_process(ngx_cycle_t *cycle)
{
    ngx_log_error(NGX_LOG_INFO, cycle->log, 0,
                  "traction: exiting process %P", ngx_pid);
}

static ngx_http_module_t traction_module_ctx = {
    NULL,              // preconfiguration
    traction_init,     // postconfiguration
    NULL,              // create_main_conf
    NULL,              // init_main_conf
    NULL,              // create_srv_conf
    NULL,              // merge_srv_conf
    NULL,              // create_loc_conf
    NULL               // merge_loc_conf
};

ngx_module_t ngx_http_traction_control_module = {
    NGX_MODULE_V1,
    &traction_module_ctx,
    NULL,                                    // directives
    NGX_HTTP_MODULE,                         // type
    NULL,                                    // init_master
    traction_init_process,                   // init_process - CRÍTICO!
    NULL,                                    // init_thread
    NULL,                                    // exit_thread
    traction_exit_process,                   // exit_process
    NULL,                                    // exit_master
    NGX_MODULE_V1_PADDING
};