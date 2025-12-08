#pragma once
#include <stdint.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

extern _Atomic(uint32_t) websocket_client_count;

/**
 * @brief 
 * 
 * @return the client count \e before the increment.
 */
static inline uint32_t websocket_client_added(void) {
    const uint32_t cnt = atomic_fetch_add(&websocket_client_count,1);
    return cnt;
}

/**
 * @brief 
 * 
 * @return the client count \e after the decrement 
 */
static inline uint32_t websocket_client_removed(void) {
    const uint32_t cnt = atomic_fetch_sub_explicit(&websocket_client_count,1,memory_order_relaxed) - 1;
    return cnt;
}

static inline uint32_t websocket_get_client_count(void) {
    return atomic_load_explicit(&websocket_client_count,memory_order_relaxed);
}

#ifdef __cplusplus
}
#endif