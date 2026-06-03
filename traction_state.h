#ifndef TRACTION_STATE_H
#define TRACTION_STATE_H

#include "traction_config.h"
#include "traction_score.h"

typedef enum {
    TRACTION_STATE_NORMAL = 0,
    TRACTION_STATE_WARNING,
    TRACTION_STATE_CRITICAL,
    TRACTION_STATE_EMERGENCY,
    TRACTION_STATE_RECOVERY
} traction_state_e;

traction_state_e  traction_get_state(ngx_http_traction_loc_conf_t *conf,
                                     double score,
                                     traction_zone_shm_t *shm);
const char       *traction_state_name(traction_state_e state);

#endif
