#ifndef BM1397_H_
#define BM1397_H_

#include "mining.h"
#include "asic_drvr.h"
#include "global_state.h"

#define BM1397_SERIALTX_DEBUG true
#define BM1397_SERIALRX_DEBUG false
#define BM1397_DEBUG_WORK false //causes insane amount of debug output
#define BM1397_DEBUG_JOBS false //causes insane amount of debug output

extern const AsicDrvr_t BM1397_drvr;

unsigned BM1397_get_compatibility(const uint16_t chip_id);
// uint32_t BM1397_get_pref_num_midstates(void);
uint8_t BM1397_init(float frequency, uint16_t asic_count, uint16_t difficulty);
void BM1397_set_diff_mask(uint32_t difficulty);
void BM1397_send_work(GlobalState * GLOBAL_STATE, bm_job * next_bm_job);
void BM1397_set_version_mask(uint32_t version_mask);
int BM1397_set_max_baud(void);
int BM1397_set_default_baud(void);
void BM1397_send_hash_frequency(float frequency);
task_result * BM1397_process_work(GlobalState * GLOBAL_STATE);
uint32_t BM1397_get_job_frequency_ms(GlobalState*);

#endif /* BM1397_H_ */
