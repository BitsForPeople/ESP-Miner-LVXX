#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include "mining.h"
#include "utils.h"
#include "mbedtls/sha256.h"

#include "mem_search.h"

#include <esp_log.h>
#include "bm_job_pool.h"

static const char* const TAG = "mining";


static inline bool isCont(uint32_t mask) {
    // The mask bits are contiguous (i.e. not ...101...) iff adding 1 to its LSB
    // flips all the mask's bits to 0.
    const uint32_t lsb = mask & -mask;
    return ((mask + lsb) & mask) == 0;
}

uint32_t mining_get_rolled_version(uint32_t version, uint32_t toAdd, uint32_t mask) {
    if((mask != 0) && (toAdd != 0)) {
        if(isCont(mask)) {
            // Easy, just add.
            const uint32_t sh = __builtin_ctz(mask);
            const uint32_t v = version + (toAdd << sh); // (version >> sh) + toAdd;
            version = (version & ~mask) | (v & mask);
        } else {
            // Bit-wise addition:
            while((toAdd != 0) && (mask != 0)) {
                // Skip over trailing 0s from toAdd
                while(((toAdd & 1) == 0) && (mask != 0)) {
                    // mask = mask ^ (mask & -mask); // clear lowest mask bit
                    mask = mask & (mask-1); // clear lowest mask bit
                    toAdd = toAdd >> 1;
                }
                if(mask != 0) {
                    const uint32_t bit = mask & -mask;
                    mask = mask ^ bit;
                    const uint32_t v = version;
                    version = version ^ bit;
                    toAdd += (version < v); // We know that toAdd & 1 == 1 here, so adding 2 is equivalent to adding just 1
                    // if(version < v) {
                    //     // Overflow
                    //     toAdd += 2;
                    // }
                    toAdd = toAdd >> 1;
                }
            }
        }
    }
    return version;
}




void free_bm_job(bm_job *job)
{
    free(job->jobid);
    free(job->extranonce2);
    // free(job);
    bmjobpool_put(job);
}

char *construct_coinbase_tx(const char *coinbase_1, const char *coinbase_2,
                            const char *extranonce, const char *extranonce_2)
{
    int coinbase_tx_len = mem_strlen(coinbase_1) + mem_strlen(coinbase_2) + mem_strlen(extranonce) + mem_strlen(extranonce_2) + 1;

    ESP_LOGI(TAG, "Coinbase TX: %d bytes", coinbase_tx_len/2);

    char *coinbase_tx = malloc(coinbase_tx_len);
    strcpy(coinbase_tx, coinbase_1);
    strcat(coinbase_tx, extranonce);
    strcat(coinbase_tx, extranonce_2);
    strcat(coinbase_tx, coinbase_2);
    coinbase_tx[coinbase_tx_len - 1] = '\0';

    return coinbase_tx;
}

static inline uint32_t min(const uint32_t a, const uint32_t b) {
    return (a<b) ? a : b;
}


MemSpan_t construct_coinbase_tx_bin(const char* const coinbase_1, const char* const coinbase_2,
                            const char* const extranonce, const char* const extranonce_2, const MemSpan_t out_cb) {
// {
//     {
//         uint32_t coinbase_tx_len = mem_strlen(coinbase_1) + mem_strlen(coinbase_2) + mem_strlen(extranonce) + mem_strlen(extranonce_2);

//         if(out_cb.size < coinbase_tx_len/2) {
//             ESP_LOGE(TAG, "Cannot construct coinbase of %" PRIu32 " bytes in buffer of %" PRIu32 " bytes.",
//                 coinbase_tx_len/2,
//                 (uint32_t)out_cb.size);
//         } else {
//             ESP_LOGI(TAG, "Coinbase TX: %" PRIu32 " bytes", coinbase_tx_len/2);
//         }
//     }

    MemSpan_t result;
    memspan_clear(&result);

    uint8_t* out = out_cb.start_u8;
    uint32_t max_out = out_cb.size;
    
    if(max_out > 0) {
        const uint32_t l = hex2bin(coinbase_1,out,max_out);
        max_out -= l;
        out += l;
    } else {
        return result;
    }

    if(max_out > 0) {
        const uint32_t l = hex2bin(extranonce,out,max_out);
        max_out -= l;
        out += l;
    } else {
        return result;
    }

    if(max_out > 0) {
        const uint32_t l = hex2bin(extranonce_2,out,max_out);
        max_out -= l;
        out += l;
    } else {
        return result;
    }

    if(max_out > 0) {
        uint32_t l = hex2bin(coinbase_2,out,max_out);
        max_out -= l;
        out += l;
    } else {
        return result;
    }

    result.start = out_cb.start;
    result.size = out - out_cb.start_u8;
    return result;


    // char *coinbase_tx = malloc(coinbase_tx_len);
    // strcpy(coinbase_tx, coinbase_1);
    // strcat(coinbase_tx, extranonce);
    // strcat(coinbase_tx, extranonce_2);
    // strcat(coinbase_tx, coinbase_2);
    // coinbase_tx[coinbase_tx_len - 1] = '\0';

    // return coinbase_tx;
}

void calculate_merkle_root_hash_bin(const MemSpan_t coinbase_tx,
     const HashLink_t* merkles, Hash_t* const out_hash) {

    Hash_t both_merkles[2];

    static_assert(sizeof(both_merkles) == 64);

    double_sha256_bin(coinbase_tx.start_u8, coinbase_tx.size, &both_merkles[0]);
    // free(coinbase_tx_bin);


    const HashLink_t* hl = merkles;
    while(hl != NULL) {
        cpyToHash(hl->hash.u32,&both_merkles[1]);
        double_sha256_bin((const uint8_t*)both_merkles, sizeof(both_merkles), &both_merkles[0]);
        hl = hl->next;
    }
    // for (int i = 0; i < num_merkle_branches; i++)
    // {
    //     cpyToHash(merkle_branches[i],&both_merkles[1]);
    //     double_sha256_bin((const uint8_t*)both_merkles, sizeof(both_merkles), &both_merkles[0]);
    // }

    cpyHashTo(&both_merkles[0],out_hash);

}

void calculate_merkle_root_hash2(const char *coinbase_tx, const HashLink_t* merkles, Hash_t* const out_hash)
{
    size_t coinbase_tx_bin_len = strlen(coinbase_tx) / 2;
    uint8_t *coinbase_tx_bin = malloc(coinbase_tx_bin_len);
    hex2bin(coinbase_tx, coinbase_tx_bin, coinbase_tx_bin_len);

    Hash_t both_merkles[2];

    static_assert(sizeof(both_merkles) == 64);

    double_sha256_bin(coinbase_tx_bin, coinbase_tx_bin_len, &both_merkles[0]);
    free(coinbase_tx_bin);


    const HashLink_t* hl = merkles;
    while(hl != NULL) {
        cpyToHash(hl->hash.u32,&both_merkles[1]);
        double_sha256_bin((const uint8_t*)both_merkles, sizeof(both_merkles), &both_merkles[0]);
        hl = hl->next;
    }
    // for (int i = 0; i < num_merkle_branches; i++)
    // {
    //     cpyToHash(merkle_branches[i],&both_merkles[1]);
    //     double_sha256_bin((const uint8_t*)both_merkles, sizeof(both_merkles), &both_merkles[0]);
    // }

    cpyHashTo(&both_merkles[0],out_hash);
}




void calculate_merkle_root_hash(const char *coinbase_tx, const uint8_t merkle_branches[][32], const int num_merkle_branches, Hash_t* const out_hash)
{
    size_t coinbase_tx_bin_len = strlen(coinbase_tx) / 2;
    uint8_t *coinbase_tx_bin = malloc(coinbase_tx_bin_len);
    hex2bin(coinbase_tx, coinbase_tx_bin, coinbase_tx_bin_len);

    Hash_t both_merkles[2];

    static_assert(sizeof(both_merkles) == 64);

    double_sha256_bin(coinbase_tx_bin, coinbase_tx_bin_len, &both_merkles[0]);
    free(coinbase_tx_bin);


    for (int i = 0; i < num_merkle_branches; i++)
    {
        cpyToHash(merkle_branches[i],&both_merkles[1]);
        double_sha256_bin((const uint8_t*)both_merkles, sizeof(both_merkles), &both_merkles[0]);
    }

    cpyHashTo(&both_merkles[0],out_hash);
}

// take a mining_notify struct with ascii hex strings and convert it to a bm_job struct
// bm_job construct_bm_job(mining_notify *params, const Hash_t* const merkle_root, const uint32_t version_mask, const uint32_t difficulty)
void construct_bm_job(mining_notify *params, const Hash_t* const merkle_root, const uint32_t version_mask, const uint32_t difficulty, bm_job* const out_job)
{
    out_job->version = params->version;
    out_job->target = params->target;
    out_job->ntime = params->ntime;
    out_job->starting_nonce = 0;
    out_job->pool_diff = difficulty;

    cpyHashTo(merkle_root,out_job->merkle_root);

    // flip32bytes(out_job->merkle_root_be,merkle_root);
    // reverse_bytes(out_job->merkle_root_be, 32);

    swap_endian_words(params->prev_block_hash, out_job->prev_block_hash);

    // hex2bin(params->prev_block_hash, out_job->prev_block_hash_be, 32);
    // reverse_bytes(out_job->prev_block_hash_be, 32);

// TODO: This is unnecessary when the ASIC doesn't need a midstate:

    ////make the midstate hash
    uint8_t midstate_data[64];

    // copy 68 bytes header data into midstate (and deal with endianess)
    memcpy(midstate_data, &out_job->version, 4);             // copy version
    memcpy(midstate_data + 4, out_job->prev_block_hash, 32); // copy prev_block_hash
    memcpy(midstate_data + 36, out_job->merkle_root, 28);    // copy merkle_root

    midstate_sha256_bin(midstate_data, 64, out_job->midstate); // make the midstate hash
    reverse_bytes(out_job->midstate, 32);                      // reverse the midstate bytes for the BM job packet

    if (version_mask != 0)
    {
        uint32_t rolled_version = increment_bitmask(out_job->version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, out_job->midstate1);
        reverse_bytes(out_job->midstate1, 32);

        rolled_version = increment_bitmask(rolled_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, out_job->midstate2);
        reverse_bytes(out_job->midstate2, 32);

        rolled_version = increment_bitmask(rolled_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, out_job->midstate3);
        reverse_bytes(out_job->midstate3, 32);
        out_job->num_midstates = 4;
    }
    else
    {
        out_job->num_midstates = 1;
    }

    // return new_job;
}

char *extranonce_2_generate(uint64_t extranonce_2, uint32_t length)
{
    // Allocate the output string
    char* const extranonce_2_str = malloc(length * 2 + 1);
    if (extranonce_2_str == NULL) {
        // free(extranonce_2_bytes);
        return NULL;
    }

    // uint32_t l1 = length <= sizeof(extranonce_2) ? length : sizeof(extranonce_2);
    // bin2hex((const uint8_t*)&extranonce_2, length, extranonce_2_str, length * 2 + 1);

    // char* p = extranonce_2_str + l1;
    // while(l1 < length) {
    //     p[0] = '0';
    //     p[1] = '0';
    //     p += 2;
    //     l1 += 1;
    // }
    // *p = 0;

    // return extranonce_2_str;

    // if(length <= sizeof(extranonce_2)) {
    //     bin2hex((const uint8_t*)&extranonce_2, length, extranonce_2_str, length * 2 + 1);
    //     return extranonce_str;
    // } else {
        // Allocate buffer to hold the extranonce_2 value in bytes
        uint8_t *extranonce_2_bytes = calloc(length, 1);
        if (extranonce_2_bytes == NULL) {
            free(extranonce_2_str);
            return NULL;
        }
        
        // Copy the extranonce_2 value into the buffer, handling endianness
        // Copy up to the size of uint64_t or the requested length, whichever is smaller
        size_t copy_len = (length < sizeof(uint64_t)) ? length : sizeof(uint64_t);
        memcpy(extranonce_2_bytes, &extranonce_2, copy_len);
          
        // Convert the bytes to hex string
        bin2hex(extranonce_2_bytes, length, extranonce_2_str, length * 2 + 1);
        
        free(extranonce_2_bytes);
        return extranonce_2_str;
    // }
}

///////cgminer nonce testing
/* truediffone == 0x00000000FFFF0000000000000000000000000000000000000000000000000000
 */

// struct Hash {
//     union {
//         uint8_t u8[32];
//         uint32_t h[8];
//     };
// };
// typedef struct Hash Hash_t;

// _Static_assert(sizeof(Hash_t) == 32);

// static inline void cpy32(const void* const src, void* const dst) {
//     const uint64_t* s = (const uint64_t*)src;
//     uint64_t* d = (uint64_t*)dst;
//     uint64_t* const end = d + 4;
//     do {
//         *d = *s;
//         ++d;
//         ++s;
//     } while(d < end);
// }

// static inline void cpyToHash(const void* const src, Hash_t* const hash) {
//     // const uint64_t* const s = (const uint64_t*)src;
//     // uint64_t* const d = (uint64_t*)hash->h;
//     // d[0] = s[0];
//     // d[1] = s[1];
//     // d[2] = s[2];
//     // d[3] = s[3];
//     // const uint32_t* const s = (const uint32_t*)src;
//     // uint32_t* const d = hash->h;
//     // for(unsigned i = 0; i < 8; ++i) {
//     //     d[i] = s[i];
//     // }
//     // cpy32(src,hash->h);
//     memcpy(hash->h, src, sizeof(Hash_t));
// }

// static inline void cpyHashTo(const Hash_t* const hash, void* const dst) {
//     uint64_t* const d = (uint64_t*)dst;
//     const uint64_t* const s = (const uint64_t*)hash->h;
//     d[0] = s[0];
//     d[1] = s[1];
//     d[2] = s[2];
//     d[3] = s[3];
// }



// static uint32_t get_hash_diff(const Hash_t* const hash) {
//     const uint32_t D1 = 0xffff0000ul;
//     // const uint32_t* h = (const uint32_t*)hash;
//     if (hash->h[7] != 0) {
//         return 0;
//     } else
//     if (hash->h[6] == 0) {
//         return 0xfffffffful;
//     } else {
//         return D1 / hash->h[6];
//     }
// }

void mining_hash_block(const BlockHeader_t* const hdr, Hash_t* const hash) {
    mbedtls_sha256((const uint8_t*)hdr, sizeof(BlockHeader_t), hash->u8, 0);
    mbedtls_sha256(hash->u8, sizeof(Hash_t), hash->u8, 0);
}

void mining_job_to_header(const bm_job* const job, const uint32_t nonce, const uint32_t rolled_version, BlockHeader_t* const hdr) {
    hdr->version = rolled_version;
    cpyToHash(job->prev_block_hash,&hdr->prev_block_hash);
    cpyToHash(job->merkle_root,&hdr->merkle_root);
    hdr->ntime = job->ntime;
    hdr->target = job->target;
    hdr->nonce = nonce;   
}

typedef struct NBits {
    union {
        uint32_t u32;
        struct {
            uint32_t m : 24;
            uint32_t e : 8;
        };
    };
} NBits_t;

static void normalize(NBits_t* nb) {
    uint32_t m = nb->m;
    if(m != 0) {
        const unsigned zerobytes = (__builtin_clz(m) / 8) - 1;
        if(zerobytes != 0) {
            const unsigned e = nb->e;    
            if(e != 0) {    
                const unsigned s = (e < zerobytes) ? e : zerobytes;
                nb->m = m << (s*8);
                nb->e = e - s;
            }
        }
    }
}

int mining_compare_nbits(const uint32_t nb_a, const uint32_t nb_b) {
    NBits_t a = {.u32 = nb_a};
    NBits_t b = {.u32 = nb_b};
    normalize(&a);
    normalize(&b);
    int c = a.e;
    c -= b.e;
    if(c==0) {
        c = a.m;
        c -= b.m;
    }
    c = (c < -1) ? -1 : c;
    c = (1 < c) ? 1 : c;
    return c;
}

uint32_t mining_get_hash_nbits(const Hash_t* const hash) {

    uint32_t exp = 32;

    const uint32_t* const h = hash->h;
    const uint32_t* ptr = h+7;
    uint32_t u32 = 0;
    while ((ptr >= h) && ((u32 = *ptr) == 0)) {
        exp -= 4;
        ptr -= 1;
    }
    if(u32 != 0) {
        uint32_t bytecnt = __builtin_clz(u32) / 8;

        if(bytecnt == 0) {
            u32 = u32 >> 8;
        } else 
        if(bytecnt >= 2) {
            u32 = u32 << ((bytecnt-1) * 8);
        }
        if(u32 & (1u<<23)) {
            // Bitcoin Core doesn't like the mantissa with MSB set, so:
            u32 = u32 >> 8;
            bytecnt -= 1;
        }
        exp -= bytecnt;
    }

    return (exp << 24) | u32;
}



/* testing a nonce and return the diff - 0 means invalid */
uint64_t test_nonce_value(const bm_job* const job, const uint32_t nonce, const uint32_t rolled_version)
{
    BlockHeader_t hdr;

    mining_job_to_header(job,nonce,rolled_version,&hdr);
    
    // Just re-using the memory in the header to store the computed hashes:
    Hash_t* const hashBuf = &hdr.prev_block_hash;

    mining_hash_block(&hdr,hashBuf); 

    return mining_get_hash_diff_u64(hashBuf);
}

// static inline uint32_t getfirstset(const uint32_t x) {
//     return x & -x;
// }
// uint32_t increment_bitmask(uint32_t value, uint32_t mask) {
//     if(mask != 0) {
//         uint32_t v;
//         do {
//             const uint32_t bit = getfirstset(mask);
//             v = value;
//             value = value ^ bit; // Toggle the bit in value
//             mask = mask ^ bit; // clear the bit in mask
//         } while(value < v && mask != 0); // If toggling cleared the bit in value (overflow), repeat with the next eligible bit from mask.
//     }
//     return value;
// }



// uint32_t increment_bitmask(const uint32_t value, const uint32_t mask)
// {
//     // if mask is zero, just return the original value
//     if (mask == 0)
//         return value;

//     uint32_t carry = (value & mask) + (mask & -mask);      // increment the least significant bit of the mask
//     uint32_t overflow = carry & ~mask;                     // find overflowed bits that are not in the mask
//     uint32_t new_value = (value & ~mask) | (carry & mask); // set bits according to the mask

//     // Handle carry propagation
//     if (overflow > 0)
//     {
//         uint32_t carry_mask = (overflow << 1);                // shift left to get the mask where carry should be propagated
//         new_value = increment_bitmask(new_value, carry_mask); // recursively handle carry propagation
//     }

//     return new_value;
// }
