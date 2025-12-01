#ifndef BM1370_H_
#define BM1370_H_

#include "common.h"
#include "mining.h"
#include "asic_drvr.h"
#include "global_state.h"

#define BM1370_SERIALTX_DEBUG false
#define BM1370_SERIALRX_DEBUG false
#define BM1370_DEBUG_WORK false //causes insane amount of debug output
#define BM1370_DEBUG_JOBS false //causes insane amount of debug output

extern const AsicDrvr_t BM1370_drvr;

unsigned BM1370_get_compatibility(uint16_t chip_id);
uint8_t BM1370_init(float frequency, uint16_t asic_count, uint16_t difficulty);
void BM1370_send_work(GlobalState * GLOBAL_STATE, bm_job * next_bm_job);
void BM1370_set_version_mask(uint32_t version_mask);
int BM1370_set_max_baud(void);
int BM1370_set_default_baud(void);
void BM1370_send_hash_frequency(float frequency);
task_result * BM1370_process_work(GlobalState * GLOBAL_STATE);
uint32_t BM1370_get_job_frequency_ms(GlobalState*);

#endif /* BM1370_H_ */
