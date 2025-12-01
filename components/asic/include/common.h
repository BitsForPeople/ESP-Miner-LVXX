#ifndef COMMON_H_
#define COMMON_H_

#include <stdint.h>
#include "esp_err.h"

typedef struct __attribute__((__packed__))
{
    uint8_t job_id;
    uint32_t nonce;
    uint32_t rolled_version;
} task_result;


/**
 * @brief Queries and identifies all ASIC chips present.
 * 
 * @param[out] out_chip_id 
 * @return The number of ASICs found. > 0 upon success, <= 0 upon error. If <tt>result <= 0</tt>, the problem occured
 * at chip number <tt>1-result</tt>.
 */
int ASIC_detect(uint16_t* const out_chip_id);
int count_asic_chips(uint16_t asic_count, uint16_t chip_id, int chip_id_response_length);
esp_err_t receive_work(uint8_t * buffer, int buffer_size);

static inline void get_difficulty_mask(const uint32_t difficulty, uint8_t* const job_difficulty_mask)
{
    // The mask must be a power of 2 so there are no holes
    // Correct:   {0b00000000, 0b00000000, 0b11111111, 0b11111111}
    // Incorrect: {0b00000000, 0b00000000, 0b11100111, 0b11111111}

    job_difficulty_mask[0] = 0x00;
    job_difficulty_mask[1] = 0x14; // TICKET_MASK

    // convert difficulty into char array
    // Ex: 256 = {0b00000000, 0b00000000, 0b00000000, 0b11111111}, {0x00, 0x00, 0x00, 0xff}
    // Ex: 512 = {0b00000000, 0b00000000, 0b00000001, 0b11111111}, {0x00, 0x00, 0x01, 0xff}

    // Running on little-endian comes in handy here:
    // We just shift all leading 0 bits from difficulty into the trailing bits of the mask.
    *(uint32_t*)(job_difficulty_mask+2) = 0xffffffffu << (__builtin_clz(difficulty)+1);
}

#endif /* COMMON_H_ */
