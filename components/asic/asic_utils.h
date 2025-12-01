#pragma once
#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus 
extern "C" {
#endif

esp_err_t ASIC_receive_work(uint8_t * buffer, int buffer_size);
void ASIC_cpy_hash_reverse_words(const void* const src, void* dst);

static inline void ASIC_get_difficulty_mask(const uint32_t difficulty, uint8_t* const job_difficulty_mask)
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

#ifdef __cplusplus 
}
#endif