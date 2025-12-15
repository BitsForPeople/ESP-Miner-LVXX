#pragma once
#include <stdint.h>
#include <stdatomic.h>
#ifdef __cpluspluc
extern "C" {
#endif

// typedef struct ObjPoolStats {
//     uint32_t allocCnt;
//     uint32_t inUseCnt;
//     uint32_t maxInUseCnt;
// } ObjPoolStats_t;

typedef struct mempool_pool {
    _Atomic (void*) dummy;
} mempool_pool_t;

void mempool_init(mempool_pool_t* pool);
void* mempool_take(mempool_pool_t* pool);
void mempool_put(mempool_pool_t* pool, void* obj);
void mempool_deinit(mempool_pool_t* pool);

#ifdef __cpluspluc
}
#endif
