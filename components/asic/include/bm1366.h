#ifndef BM1366_H_
#define BM1366_H_

#ifdef __cplusplus
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    extern "C" {
#endif

#include "mining.h"
#include "global_state.h"

#include "asic_drvr.h"

#define BM1366_SERIALTX_DEBUG false
#define BM1366_SERIALRX_DEBUG false
#define BM1366_DEBUG_WORK false //causes insane amount of debug output
#define BM1366_DEBUG_JOBS false //causes insane amount of debug output

extern const AsicDrvr_t BM1366_drvr;

unsigned BM1366_get_compatibility(uint16_t chip_id);
uint8_t BM1366_init(float frequency, uint16_t asic_count, uint16_t difficulty);
void BM1366_send_work(GlobalState* GLOBAL_STATE, bm_job * next_bm_job);
void BM1366_set_version_mask(uint32_t version_mask);
int BM1366_set_max_baud(void);
int BM1366_set_default_baud(void);
void BM1366_send_hash_frequency(float frequency);
task_result * BM1366_process_work(GlobalState* GLOBAL_STATE);
uint32_t BM1366_get_job_frequency_ms(GlobalState*);

#ifdef __cplusplus
    } // extern "C"
    #pragma GCC diagnostic pop
#endif

#endif /* BM1366_H_ */
