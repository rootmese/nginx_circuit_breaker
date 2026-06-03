#include "traction_state.h"

traction_state_e
traction_get_state(ngx_http_traction_loc_conf_t *conf, double score,
                   traction_zone_shm_t *shm)
{
    traction_state_e  prev_state = TRACTION_STATE_NORMAL;
    ngx_atomic_t      raw_state;

    if (shm != NULL) {
        raw_state = ngx_atomic_fetch_add(&shm->last_state, 0);
        prev_state = (traction_state_e) raw_state;
    }

    if (score < (double) conf->emergency_threshold) {
        return TRACTION_STATE_EMERGENCY;
    }

    if (score < (double) conf->critical_threshold) {
        if (prev_state == TRACTION_STATE_EMERGENCY
            || prev_state == TRACTION_STATE_RECOVERY)
        {
            return TRACTION_STATE_RECOVERY;
        }

        return TRACTION_STATE_CRITICAL;
    }

    if (score < (double) conf->warning_threshold) {
        return TRACTION_STATE_WARNING;
    }

    return TRACTION_STATE_NORMAL;
}

const char *
traction_state_name(traction_state_e state)
{
    switch (state) {
    case TRACTION_STATE_WARNING:
        return "warning";
    case TRACTION_STATE_CRITICAL:
        return "critical";
    case TRACTION_STATE_EMERGENCY:
        return "emergency";
    case TRACTION_STATE_RECOVERY:
        return "recovery";
    default:
        return "normal";
    }
}
