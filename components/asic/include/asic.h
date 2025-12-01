#ifndef ASIC_H
#define ASIC_H

#include <esp_err.h>
#include "global_state.h"
#include "common.h"
#include "asic_drvr.h"

uint8_t ASIC_init(GlobalState * GLOBAL_STATE);

static inline const char* ASIC_get_driver_name(const GlobalState* const GLOBAL_STATE) {
    return GLOBAL_STATE->asic_drvr->name;
}

static inline uint32_t ASIC_get_hashes_per_clock(const GlobalState* const GLOBAL_STATE) {
    return GLOBAL_STATE->asic_drvr->hashes_per_clock;
}

static inline task_result* ASIC_process_work(GlobalState* const GLOBAL_STATE) {
    return GLOBAL_STATE->asic_drvr->process_work(GLOBAL_STATE);
}

static inline int ASIC_set_max_baud(const GlobalState* const GLOBAL_STATE) {
    return GLOBAL_STATE->asic_drvr->set_max_baud();
}

static inline void ASIC_send_work(GlobalState* const GLOBAL_STATE, void* const next_job) {
    GLOBAL_STATE->asic_drvr->send_work(GLOBAL_STATE, (bm_job*)next_job);
}

static inline bool ASIC_has_version_rolling(const GlobalState* const GLOBAL_STATE) {
    return GLOBAL_STATE->asic_drvr->set_version_mask != NULL;
}

static inline bool ASIC_set_version_mask(const GlobalState* const GLOBAL_STATE, uint32_t mask) {
    if(ASIC_has_version_rolling(GLOBAL_STATE)) {
        GLOBAL_STATE->asic_drvr->set_version_mask(mask);
        return true;
    } else {
        return false;
    }
}

bool ASIC_set_frequency(GlobalState * GLOBAL_STATE, float target_frequency);

static inline double ASIC_get_asic_job_frequency_ms(GlobalState* const GLOBAL_STATE)
{
    return GLOBAL_STATE->asic_drvr->get_job_frequency_ms(GLOBAL_STATE);
}

#endif // ASIC_H
