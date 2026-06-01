#ifndef TRACTION_METRICS_H
#define TRACTION_METRICS_H

#include "traction_shared_memory.h"

void  traction_record_request(traction_zone_shm_t *shm);
void  traction_record_error(traction_zone_shm_t *shm);

#endif
