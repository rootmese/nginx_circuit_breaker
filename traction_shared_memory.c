#include "traction_shared_memory.h"
#include "traction_config.h"
#include "traction_state.h"

extern ngx_module_t  ngx_http_traction_control_module;

static ngx_int_t
traction_zone_init(ngx_shm_zone_t *shm_zone, void *data)
{
    traction_zone_conf_t  *conf;
    traction_zone_shm_t   *shm;
    size_t                 size;

    conf = shm_zone->data;

    if (data) {
        shm_zone->data = data;
        return NGX_OK;
    }

    if (conf == NULL || conf->window == 0) {
        return NGX_ERROR;
    }

    size = traction_zone_shm_size(conf->window);
    shm = (traction_zone_shm_t *) shm_zone->shm.addr;

    ngx_memzero(shm, size);
    shm->window = conf->window;
    shm->last_state = TRACTION_STATE_NORMAL;
    shm_zone->data = shm;

    ngx_log_error(NGX_LOG_NOTICE, shm_zone->shm.log, 0,
                  "traction: zone shared memory ready (%uz bytes, %ui buckets)",
                  size, conf->window);

    return NGX_OK;
}

ngx_int_t
traction_zones_setup(ngx_cycle_t *cycle)
{
    ngx_http_traction_main_conf_t  *tmcf;
    ngx_http_traction_zone_t       *zones;
    ngx_uint_t                      i;

    if (cycle->conf_ctx == NULL) {
        return NGX_OK;
    }

    tmcf = ngx_http_cycle_get_module_main_conf(cycle,
                                               ngx_http_traction_control_module);
    if (tmcf == NULL || tmcf->zones.nelts == 0) {
        return NGX_OK;
    }

    zones = tmcf->zones.elts;

    for (i = 0; i < tmcf->zones.nelts; i++) {
        if (zones[i].shm_zone == NULL) {
            continue;
        }

        zones[i].shm = zones[i].shm_zone->data;

        if (zones[i].shm == NULL) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "traction: zone \"%V\" shared memory not initialized",
                          &zones[i].name);
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

ngx_int_t
traction_zone_register(ngx_conf_t *cf, ngx_http_traction_zone_t *zone,
    size_t size)
{
    ngx_str_t             name;
    size_t                need;
    traction_zone_conf_t  *conf;
    ngx_shm_zone_t       *shm_zone;

    conf = ngx_pcalloc(cf->pool, sizeof(traction_zone_conf_t));
    if (conf == NULL) {
        return NGX_ERROR;
    }

    conf->window = zone->window;
    need = traction_zone_shm_size(zone->window);

    if (size < need) {
        size = need;
    }

    name.len = zone->name.len + sizeof("traction_zone_") - 1;
    name.data = ngx_pnalloc(cf->pool, name.len);
    if (name.data == NULL) {
        return NGX_ERROR;
    }

    ngx_snprintf(name.data, name.len, "traction_zone_%V", &zone->name);

    shm_zone = ngx_shared_memory_add(cf, &name, size,
                                     &ngx_http_traction_control_module);
    if (shm_zone == NULL) {
        return NGX_ERROR;
    }

    shm_zone->data = conf;
    shm_zone->init = traction_zone_init;
    zone->shm_zone = shm_zone;

    return NGX_OK;
}
