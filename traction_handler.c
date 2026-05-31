#include "traction_metrics.h"
#include "traction_score.h"
#include "traction_decision.h"

ngx_int_t
traction_handler(ngx_http_request_t *r)
{
    double score;

    traction_record_request();

    score = traction_calculate_score();

    return traction_decide(score);
}