#include <atomic>
#include "mempool.hpp"
#include "bm_job_pool.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include "mempool_alloc_fn.hpp"

static constexpr const char* TAG = "bmjobpool";

static constexpr uint32_t GROW_CNT = 4;

using pool_t = mempool::GrowingMemPool<bm_job,GROW_CNT,mempool::alloc::PREFER_PSRAM>;

static pool_t pool {};

static std::atomic<uint32_t> takenCnt {};
static std::atomic<uint32_t> maxInUse {};

bool bmjobpool_grow_by(const size_t cnt) {
    return pool.growBy(cnt);
}

uint32_t bmjobpool_get_size(void) {
    return pool.getSize();
}

void bmjobpool_put(bm_job* const obj) {
    if(obj) [[likely]] {
        pool.put(obj);
        takenCnt.fetch_sub(1,std::memory_order::relaxed);
    }
}

bm_job* bmjobpool_take() {
    bm_job* h = pool.take();

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


uint32_t bmjobpool_getMaxUse() {
    return maxInUse.load(std::memory_order::relaxed);
}

void bmjobpool_get_stats(ObjPoolStats_t* const out_stats) {
    out_stats->allocCnt = pool.getSize();
    out_stats->inUseCnt = takenCnt.load(std::memory_order::relaxed);
    out_stats->maxInUseCnt = maxInUse.load(std::memory_order::relaxed);
}

void bmjobpool_log_stats(void) {
    ObjPoolStats_t stats;
    bmjobpool_get_stats(&stats);
    ESP_LOGI(TAG, "Pool stats: in use: %" PRIu32 "/%" PRIu32 ", max in use: %" PRIu32,
        stats.inUseCnt,
        stats.allocCnt,
        stats.maxInUseCnt
    );
}