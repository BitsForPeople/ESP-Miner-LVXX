#pragma once
#include <stdint.h>
#include <assert.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif



struct Hash {
    union {
        uint8_t u8[32];
        uint32_t h[8];
        uint32_t u32[8];
    };
};
typedef struct Hash Hash_t;

static_assert(sizeof(Hash_t) == 32);


typedef struct HashLink {
    struct HashLink* next;
    Hash_t hash;
} HashLink_t;




typedef struct Nonce {
    union {
        uint64_t u64[2];
        uint32_t u32[4];
        uint8_t u8[16];
    };
    uint8_t size;
} Nonce_t;

#define JOB_ID_MAX_LEN (24)

typedef struct JobId {
    char idstr[JOB_ID_MAX_LEN + 1];
    uint8_t len;
} JobId_t;


typedef struct MemSpan {
    union {
        void* start;
        uint8_t* start_u8;
        uint32_t* start_u32;
        char* start_str;
    };
    size_t size;
} MemSpan_t;

static inline void memspan_clear(MemSpan_t* span) {
    span->start = NULL;
    span->size = 0;
}

static inline MemSpan_t memspan_get(const void* start, const size_t sz) {
    const MemSpan_t s = {.start = (void*)start, .size = sz};
    return s;
}


// typedef struct CoinbaseTxBuf {
//     size_t size;
//     size_t len;
//     uint8_t buf[];
// } CoinbaseTxBuf;





struct BlockHeader {
    uint32_t version;
    Hash_t prev_block_hash;
    Hash_t merkle_root;
    uint32_t ntime;
    uint32_t target;
    uint32_t nonce;
};
typedef struct BlockHeader BlockHeader_t;

static_assert(sizeof(BlockHeader_t) == 80);



static inline void cpyToHash(const void* const src, Hash_t* const hash) {
    const uint64_t* const s = (const uint64_t*)src;
    uint64_t* const d = (uint64_t*)hash->h;
    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
    d[3] = s[3];
    // const uint32_t* const s = (const uint32_t*)src;
    // uint32_t* const d = hash->h;
    // for(unsigned i = 0; i < 8; ++i) {
    //     d[i] = s[i];
    // }
    // cpy32(src,hash->h);
    // memcpy(hash->h, src, sizeof(Hash_t));
}

static inline void cpyHashTo(const Hash_t* const hash, void* const dst) {
    uint64_t* const d = (uint64_t*)dst;
    const uint64_t* const s = (const uint64_t*)hash->h;
    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
    d[3] = s[3];
    // memcpy(dst,hash->h,sizeof(Hash_t));
}


#ifdef __cplusplus
}
#endif