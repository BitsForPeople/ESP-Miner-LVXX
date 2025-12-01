




#include <atomic>
#include "mempool.hpp"
#include "hashpool.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include "mempool_alloc_fn.hpp"

static constexpr const char* TAG = "hashpool";

static constexpr uint32_t GROW_CNT = 4;

using pool_t = mempool::GrowingMemPool<HashLink_t,GROW_CNT,mempool::alloc::PREFER_PSRAM>;

static pool_t pool {};

static std::atomic<uint32_t> takenCnt {};
static std::atomic<uint32_t> maxInUse {};

bool hashpool_grow_by(const size_t cnt) {
    return pool.growBy(cnt);
}

uint32_t hashpool_get_size(void) {
    return pool.getSize();
}

void hashpool_put(HashLink_t* const obj) {
    if(obj) [[likely]] {
        pool.put(obj);
        takenCnt.fetch_sub(1,std::memory_order::relaxed);
    }
}

HashLink_t* hashpool_take() {
    HashLink_t* h = pool.take();

    if(h != nullptr) [[likely]] {
        uint32_t tcnt = takenCnt.fetch_add(1,std::memory_order::relaxed) + 1;
        uint32_t mx = maxInUse.load(std::memory_order::relaxed);
        while(tcnt > mx && !maxInUse.compare_exchange_strong(mx,tcnt)) {

        }
    } else {
        ESP_LOGE(TAG, "Failed to allocate!");
    }
    return h;
}


uint32_t hashpool_getMaxUse() {
    return maxInUse.load(std::memory_order::relaxed);
}

void hashpool_get_stats(ObjPoolStats_t* const out_stats) {
    out_stats->allocCnt = pool.getSize();
    out_stats->inUseCnt = takenCnt.load(std::memory_order::relaxed);
    out_stats->maxInUseCnt = maxInUse.load(std::memory_order::relaxed);
}

void hashpool_log_stats(void) {
    ObjPoolStats_t stats;
    hashpool_get_stats(&stats);
    ESP_LOGI(TAG, "Pool stats: in use: %" PRIu32 "/%" PRIu32 ", max in use: %" PRIu32,
        stats.inUseCnt,
        stats.allocCnt,
        stats.maxInUseCnt
    );
}



// #include <atomic>
// #include "mempool.hpp"
// #include "hashpool.h"
// #include <esp_heap_caps.h>
// #include <esp_log.h>

// static constexpr const char* TAG = "hashpool";

// // using pool_t = mempool::MemPoolBase<HashLink_t>;

// // static mempool::MemPoolBase<HashLink_t> hashpool {};

// static constexpr uint32_t GROW_CNT = 8;

// // static void* allocExt(size_t align, size_t sz) {
// //     return heap_caps_aligned_alloc(align, sz, MALLOC_CAP_SPIRAM );
// // }

// static constexpr auto allocfn = [](size_t align, size_t sz) { return heap_caps_aligned_alloc(align, sz, MALLOC_CAP_SPIRAM ); };

// using pool_t = mempool::GrowingMemPool<HashLink_t,GROW_CNT,allocfn>;

// static pool_t hashpool {};

// template<typename T>
// static inline T* allocExt(uint32_t cnt) {
//     return (T*)heap_caps_aligned_alloc(alignof(T), cnt * sizeof(T), MALLOC_CAP_SPIRAM );
// }

// static std::atomic<uint32_t> takenCnt {};
// static std::atomic<uint32_t> allocCnt {};
// static std::atomic<uint32_t> maxInUse {};

// static inline void addToPool(HashLink_t* const mem, const size_t cnt) {
//     allocCnt.fetch_add(cnt,std::memory_order::relaxed);
//     for(unsigned i = 0; i < cnt; ++i) {
//         // hashpool.put(mem+i);
//         hashpool.put(mem+i);
//     }
// }

// bool hashpool_add(const size_t cnt) {

//     bool r = hashpool.growBy(cnt);
//     if(r) {
//         allocCnt.fetch_add(cnt,std::memory_order::relaxed);
//     }
//     return r;
//     // if(cnt != 0) {
//     //     HashLink_t* h = allocExt<HashLink_t>(cnt);
//     //     if(h != nullptr) {
//     //         addToPool(h,cnt);
//     //         return true;
//     //     } else {
//     //         ESP_LOGE(TAG, "Failed to allocate!");
//     //         return false;
//     //     }
//     // } else {
//     //     return true;  
//     // }
// }

// uint32_t hashpool_get_size(void) {
//     return allocCnt.load(std::memory_order::relaxed);
// }

// void hashpool_put(HashLink_t* obj) {
//     // hashpool.put(obj);
//     hashpool.put(obj);
//     takenCnt.fetch_sub(1,std::memory_order::relaxed);
// }

// HashLink_t* hashpool_take() {

//     // HashLink_t* h = hashpool.take();
//     HashLink_t* h = hashpool.take();
//     // if constexpr (GROW_CNT != 0) {
//     //     if(h == nullptr) {
//     //         ESP_LOGI(TAG, "Allocating %" PRIu32 " objects to pool.", GROW_CNT);
//     //         h = allocExt<HashLink_t>(GROW_CNT);
//     //         if(h != nullptr) {
//     //             // putCnt.fetch_add(1,std::memory_order::relaxed);
//     //             allocCnt.fetch_add(1,std::memory_order::relaxed);
//     //             // Add all but the first new object to the pool:
//     //             if constexpr (GROW_CNT > 1) {
//     //                 addToPool(h+1,GROW_CNT-1);
//     //             }
//     //         } else {
//     //             ESP_LOGE(TAG, "Failed to allocate!");
//     //         }
//     //     }
//     // }

//     if(h != nullptr) {
//         uint32_t tcnt = takenCnt.fetch_add(1,std::memory_order::relaxed) + 1;
//         // uint32_t acnt = allocCnt.load(std::memory_order::relaxed);
        
//         // uint32_t diff = acnt-tcnt;
//         uint32_t mx = maxInUse.load(std::memory_order::relaxed);
//         while(tcnt > mx && !maxInUse.compare_exchange_strong(mx,tcnt)) {

//         }
//     } else {
//         ESP_LOGE(TAG, "Failed to allocate!");
//     }
//     return h;
// }


// uint32_t hashpool_getMaxUse() {
//     return maxInUse.load(std::memory_order::relaxed);
// }

// void hashpool_get_stats(HashpoolStats_t* const out_stats) {
//     out_stats->allocCnt = allocCnt.load(std::memory_order::relaxed);
//     out_stats->inUseCnt = takenCnt.load(std::memory_order::relaxed);
//     out_stats->maxInUseCnt = maxInUse.load(std::memory_order::relaxed);
// }