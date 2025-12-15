#pragma once
#include <stdint.h>
#include "global_state.h"
#include "mining.h" // bm_job
#include "device_config.h" // enum Asic
#include "task_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Holds pointers to the respective functions for each type of ASIC (VMT-style).
 * 
 */
struct AsicDrvr {
    Asic id;
    const char* name;
    uint32_t hashes_per_clock;
    bool midstate_autogen;
    unsigned (*get_compatibility)(uint16_t chipId);
    uint8_t (*init)(float frequency, uint16_t asic_count, uint16_t difficulty);
    void (*set_diff_mask)(uint32_t difficulty);
    task_result* (*process_work)(GlobalState*);
    int (*set_max_baud)(void);
    void (*send_work)(GlobalState*, bm_job* job);
    void (*set_version_mask)(uint32_t mask);
    void (*send_frequency)(float freq);
    uint32_t (*get_job_frequency_ms)(GlobalState*);
};

typedef struct AsicDrvr AsicDrvr_t;

#ifdef __cplusplus
}
#endif
