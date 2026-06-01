#include "traction_shared_memory.h"

traction_shared_t *traction_shm = NULL;

// Ponteiro estático para a zona (precisa ser acessível globalmente)
static ngx_shm_zone_t *traction_shm_zone = NULL;

// Slot para guardar dados no ciclo (alternativa mais robusta)
static ngx_uint_t traction_shm_slot;

// Callback quando a zona é inicializada (primeira vez ou recarregamento)
static ngx_int_t
traction_shm_init_zone(ngx_shm_zone_t *shm_zone, void *data)
{
    traction_shared_t *shm = shm_zone->data;
    
    ngx_log_error(NGX_LOG_INFO, shm_zone->shm.log, 0,
                  "traction: initializing shared memory zone '%V'", 
                  &shm_zone->shm.name);
    
    if (data) {
        // Configuração existente (ngx_reload)
        shm_zone->data = data;
        ngx_log_error(NGX_LOG_INFO, shm_zone->shm.log, 0,
                      "traction: reusing existing shared memory data");
        return NGX_OK;
    }
    
    // Primeira inicialização - zera toda a memória
    ngx_memzero(shm, sizeof(traction_shared_t));
    shm->bucket_count = TRACTION_BUCKETS_DEFAULT;
    shm->last_rotate = ngx_time();
    
    ngx_log_error(NGX_LOG_INFO, shm_zone->shm.log, 0,
                  "traction: shared memory initialized with %d buckets",
                  TRACTION_BUCKETS_DEFAULT);
    
    return NGX_OK;
}

// Função que será chamada no post-configuration
static ngx_int_t
traction_post_conf(ngx_conf_t *cf)
{
    ngx_str_t name = ngx_string(TRACTION_SHM_NAME);
    ngx_shm_zone_t *shm_zone;
    ngx_http_core_main_conf_t *cmcf;
    
    // Obtém a configuração principal do http
    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    
    // Registra ou obtém a zona de memória compartilhada
    shm_zone = ngx_shared_memory_add(cf, &name, 
                                      sizeof(traction_shared_t),
                                      &ngx_http_traction_control_module);
    if (shm_zone == NULL) {
        return NGX_ERROR;
    }
    
    // Define o callback de inicialização
    shm_zone->init = traction_shm_init_zone;
    
    // Guarda referência
    traction_shm_zone = shm_zone;
    
    return NGX_OK;
}

// Inicialização no nível do módulo (chamado pelo Nginx)
ngx_int_t
traction_init_shm(ngx_conf_t *cf)
{
    // Registra hook de pós-configuração
    return traction_post_conf(cf);
}

// Função chamada no início de cada ciclo (worker ou master)
static ngx_int_t
traction_init_cycle(ngx_cycle_t *cycle)
{
    ngx_shm_zone_t *shm_zone;
    ngx_slab_pool_t *shpool;
    
    if (traction_shm_zone == NULL) {
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                      "traction: shared memory zone not found");
        return NGX_OK;
    }
    
    shm_zone = traction_shm_zone;
    
    // Pega o ponteiro para a memória compartilhada
    traction_shm = shm_zone->data;
    
    if (traction_shm == NULL) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "traction: shared memory zone not initialized");
        return NGX_ERROR;
    }
    
    ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
                  "traction: worker %P using shared memory at %p",
                  ngx_pid, traction_shm);
    
    return NGX_OK;
}

// Função para registrar o callback no ciclo (será chamada no módulo principal)
void
traction_register_shm_callback(ngx_cycle_t *cycle)
{
    ngx_core_conf_t *ccf;
    
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    
    if (ccf->master) {
        // Em modo master, registra o callback de init_cycle
        // Isso é feito através do ngx_cycle->post_conf
    }
}