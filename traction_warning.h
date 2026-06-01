#ifndef TRACTION_WARNING_H
#define TRACTION_WARNING_H

#include "traction_config.h"
#include "traction_shared_memory.h"

#define TRACTION_WARNING_ACTION_HEADERS     1
#define TRACTION_WARNING_ACTION_OFF         2
#define TRACTION_WARNING_ACTION_RATE_LIMIT  3

#define TRACTION_WARNING_REJECT_DEFAULT     30

ngx_flag_t  traction_warning_emit_headers(ngx_http_traction_loc_conf_t *conf);
ngx_flag_t  traction_warning_should_shed(ngx_http_traction_loc_conf_t *conf,
    traction_zone_shm_t *shm);
const char *traction_warning_action_name(ngx_http_traction_loc_conf_t *conf);

#endif
