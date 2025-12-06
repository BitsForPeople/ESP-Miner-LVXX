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

#define WS_EVENT_CLIENT_ADDED   (1<<0)
#define WS_EVENT_CLIENT_REMOVED (1<<1)
#define WS_EVENT_MSG_AVAIL      (1<<2)

extern EventGroupHandle_t const websocket_event_handle;

extern _Atomic(uint32_t) websocket_client_count;

/**
 * @brief 
 * 
 * @return the client count \e before the increment.
 */
static inline uint32_t websocket_client_added(void) {
    const uint32_t cnt = atomic_fetch_add(&websocket_client_count,1);
    xEventGroupSetBits(websocket_event_handle, WS_EVENT_CLIENT_ADDED);
    return cnt;
}

/**
 * @brief 
 * 
 * @return the client count \e after the decrement 
 */
static inline uint32_t websocket_client_removed(void) {
    const uint32_t cnt = atomic_fetch_sub_explicit(&websocket_client_count,1,memory_order_relaxed) - 1;
    xEventGroupSetBits(websocket_event_handle, WS_EVENT_CLIENT_REMOVED);
    return cnt;
}

static inline uint32_t websocket_get_client_count(void) {
    return atomic_load_explicit(&websocket_client_count,memory_order_relaxed);
}

static inline EventBits_t websocket_wait_for_client_event(TickType_t maxWait) {
    return xEventGroupWaitBits(websocket_event_handle, WS_EVENT_CLIENT_ADDED | WS_EVENT_CLIENT_REMOVED, true, false, maxWait);
}

static inline EventBits_t websocket_wait_for_client_added(TickType_t maxWait) {
    return xEventGroupWaitBits(websocket_event_handle, WS_EVENT_CLIENT_ADDED, true, false, maxWait);
}


#ifdef __cplusplus
}
#endif