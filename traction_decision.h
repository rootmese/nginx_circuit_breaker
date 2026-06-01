#ifndef TRACTION_DECISION_H
#define TRACTION_DECISION_H

#include "traction_config.h"

ngx_int_t  traction_decide(ngx_http_request_t *r,
                           ngx_http_traction_loc_conf_t *conf, double score);

#endif
