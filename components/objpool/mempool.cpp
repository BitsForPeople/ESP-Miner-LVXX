#include <cstdint>
#include "mempool.hpp"
#include "cmempool.h"

using dummy_obj_t = uintptr_t;
using cpool_t = mempool::MemPoolBase<dummy_obj_t>;

static_assert(sizeof(mempool_pool_t) == sizeof(cpool_t));
static_assert(alignof(mempool_pool_t) >= alignof(cpool_t));

void mempool_init(mempool_pool_t* pool) {
    new (pool) cpool_t();
}

void mempool_deinit(mempool_pool_t* pool) {
    cpool_t& p = *(cpool_t*)pool;
    p.~cpool_t();
    // ((cpool_t*)pool)->~cpool_t();
}

void* mempool_take(mempool_pool_t* pool) {
    cpool_t& p = *(cpool_t*)pool;
    return p.take();
}

void mempool_put(mempool_pool_t* pool, void* obj) {
    cpool_t& p = *(cpool_t*)pool;
    p.put((dummy_obj_t*)obj);
}

