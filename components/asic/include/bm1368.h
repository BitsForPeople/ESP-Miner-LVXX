#ifndef BM1368_H_
#define BM1368_H_

#include "mining.h"
#include "asic_drvr.h"
#include "global_state.h"

#define BM1368_SERIALTX_DEBUG false
#define BM1368_SERIALRX_DEBUG false
#define BM1368_DEBUG_WORK false //causes insane amount of debug output
#define BM1368_DEBUG_JOBS false //causes insane amount of debug output

extern const AsicDrvr_t BM1368_drvr;

unsigned BM1368_get_compatibility(uint16_t chip_id);
uint8_t BM1368_init(float frequency, uint16_t asic_count, uint16_t difficulty);
void BM1368_send_work(GlobalState * GLOBAL_STATE, bm_job * next_bm_job);
void BM1368_set_version_mask(uint32_t version_mask);
int BM1368_set_max_baud(void);
int BM1368_set_default_baud(void);
void BM1368_send_hash_frequency(float frequency);
task_result * BM1368_process_work(GlobalState * GLOBAL_STATE);
uint32_t BM1368_get_job_frequency_ms(GlobalState*);


#endif /* BM1368_H_ */
