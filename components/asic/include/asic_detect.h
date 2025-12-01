#ifndef COMMON_H_
#define COMMON_H_

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Queries and identifies all ASIC chips present.
 * 
 * @param[out] out_chip_id 
 * @return The number of ASICs found. > 0 upon success, <= 0 upon error. If <tt>result <= 0</tt>, the problem occured
 * at chip number <tt>1-result</tt>.
 */
int ASIC_detect(uint16_t* const out_chip_id);
int count_asic_chips(uint16_t asic_count, uint16_t chip_id, int chip_id_response_length);

#endif /* COMMON_H_ */
