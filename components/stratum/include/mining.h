#ifndef MINING_H_
#define MINING_H_

#include "mining_types.h"
#include "stratum_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t version;
    uint32_t version_mask;
    uint8_t prev_block_hash[32];
    uint8_t merkle_root[32];
    uint32_t ntime;
    uint32_t target; // aka difficulty, aka nbits
    uint32_t starting_nonce;

    uint8_t num_midstates;
    uint8_t midstate[32];
    uint8_t midstate1[32];
    uint8_t midstate2[32];
    uint8_t midstate3[32];
    uint32_t pool_diff;
    char *jobid;
    char *extranonce2;
} bm_job;

void free_bm_job(bm_job *job);

static inline void free_bm_job_from_queue(void* item) {
    free_bm_job((bm_job*)item);
}

char *construct_coinbase_tx(const char *coinbase_1, const char *coinbase_2,
                            const char *extranonce, const char *extranonce_2);

MemSpan_t construct_coinbase_tx_bin(
                    const char* const coinbase_1, const char* const coinbase_2,
                    const char* const extranonce, const char* const extranonce_2,
                    const MemSpan_t out_cb);

void calculate_merkle_root_hash_bin(const MemSpan_t coinbase_tx,
     const HashLink_t* merkles, Hash_t* const out_hash);

void calculate_merkle_root_hash(const char *coinbase_tx, const uint8_t merkle_branches[][32], const int num_merkle_branches, Hash_t* out_hash);
void calculate_merkle_root_hash2(const char *coinbase_tx, const HashLink_t* merkles, Hash_t* const out_hash);

// bm_job construct_bm_job(mining_notify *params, const char *merkle_root, const uint32_t version_mask, uint32_t difficulty);
void construct_bm_job(mining_notify *params, const Hash_t* const merkle_root, const uint32_t version_mask, const uint32_t difficulty, bm_job* out_job);

uint64_t test_nonce_value(const bm_job *job, const uint32_t nonce, const uint32_t rolled_version);

char *extranonce_2_generate(uint64_t extranonce_2, uint32_t length);


// Version rolling helpers:

uint32_t mining_get_rolled_version(uint32_t version, uint32_t toAdd, uint32_t mask);

static inline uint32_t mining_get_mask_range_log2(uint32_t mask) {
    return __builtin_popcount(mask);
}

/**
 * @brief Returns the limit of the range a version mask supports.
 * The number of different version values permitted by the given mask, including 0,
 * is thus <tt>mining_get_mask_range_limit(mask) + 1</tt>. Note that a mask of 
 * \c 0xffffffff has a range limit of \c 0xffffffff, so 0x100000000 different values,
 * which doesn't fit into a \c uint32_t .
 * 
 * 
 * @param mask The version mask
 * @return the range limit of the mask
 */
static inline uint32_t mining_get_mask_range_limit(uint32_t mask) {
    if((mask+1) > 1) { // i.e. not 0 and not -1
        // (shifting a uint32_t by 32 bits is technically UB, so we avoid it)
        mask = (1u << mining_get_mask_range_log2(mask))-1;
    }
    return mask;
}



static inline uint32_t increment_bitmask(const uint32_t value, const uint32_t mask) {
    return mining_get_rolled_version(value,1,mask);
}



void mining_job_to_header(const bm_job *job, const uint32_t nonce, const uint32_t rolled_version, BlockHeader_t* hdr);
void mining_hash_block(const BlockHeader_t* const hdr, Hash_t* const hash);
/**
 * @brief Return the difficulty of a hash in the same 8+24-bit format as the network difficulty
 * 
 * @param hash 
 * @return uint32_t 
 */
uint32_t mining_get_hash_nbits(const Hash_t* const hash);

static inline uint64_t mining_get_hash_diff_u64(const Hash_t* const h) {
    static const uint32_t D1_32 = 0xffff0000ul;
    static const uint64_t D1_64 = (uint64_t)D1_32 << 32;
    if((h->h[7] != 0) || (h->h[6] > D1_32)) {
        return 0;
    } else 
    if(h->h[6] != 0) {
        return D1_64 / ((((uint64_t)h->h[6]) << 32) | (h->h[5]));
    } else
    if(h->h[5] != 0 || h->h[4] > D1_32) {
        return (D1_64 / ((((uint64_t)h->h[5]) << 32) | (h->h[5]))) << 32;
    } else {
        return (uint64_t)-1;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* MINING_H_ */