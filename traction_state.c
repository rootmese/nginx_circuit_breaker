#include "traction_state.h"

traction_state_e
traction_get_state(ngx_http_traction_loc_conf_t *conf, double score)
{
    if (score < (double) conf->emergency_threshold) {
        return TRACTION_STATE_EMERGENCY;
    }

    if (score < (double) conf->critical_threshold) {
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
    default:
        return "normal";
    }
}
