#pragma once
#include <stdatomic.h> // mining_notify
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "stratum_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Provides an interface for interacting with the asic_task.

// stratum -> asic
#define ASIC_TASK_EVENT_POOL_DIFF_CHANGED      (1<<0)
#define ASIC_TASK_EVENT_VERSION_MASK_CHANGED   (1<<1)
#define ASIC_TASK_EVENT_STRATUM_NEW_WORK       (1<<2)
#define ASIC_TASK_EVENT_STRATUM_XN2_CHANGED    (1<<3)

#define ASIC_TASK_EVENT_STRATUM_ABANDON_WORK   (1<<4)
// asic -> stratum
#define ASIC_TASK_EVENT_STRATUM_WORK_ABANDONED (1<<5) // response to ABANDON_WORK

#define ASIC_TASK_EVENT_STRATUM_EVENTS         (\
    ASIC_TASK_EVENT_POOL_DIFF_CHANGED | \
    ASIC_TASK_EVENT_VERSION_MASK_CHANGED | \
    ASIC_TASK_EVENT_STRATUM_NEW_WORK | \
    ASIC_TASK_EVENT_STRATUM_ABANDON_WORK | \
    ASIC_TASK_EVENT_STRATUM_XN2_CHANGED)


extern _Atomic(mining_notify*) asic_task_mining_notify;
extern EventGroupHandle_t const asic_task_event_handle;

static inline EventBits_t asic_task_event_set_bits(const EventBits_t bits) {
    return xEventGroupSetBits(asic_task_event_handle,bits);
}

// static inline EventBits_t asic_task_event_wait(const EventBits_t bitsToWaitFor, const bool clearOnExit, const TickType_t maxWait) {
//     return xEventGroupWaitBits(asic_task_event_handle,bitsToWaitFor,clearOnExit,0,maxWait);
// }

// static inline EventBits_t asic_task_event_clear_bits(const EventBits_t bits) {
//     return xEventGroupClearBits(asic_task_event_handle,bits);
// }

// static inline EventBits_t asic_task_event_get_bits(void) {
//     return xEventGroupGetBits(asic_task_event_handle);
// }

static inline EventBits_t asic_task_notify_diff_change(void) {
    return asic_task_event_set_bits(ASIC_TASK_EVENT_POOL_DIFF_CHANGED);
}

static inline EventBits_t asic_task_notify_version_change(void) {
    return asic_task_event_set_bits(ASIC_TASK_EVENT_VERSION_MASK_CHANGED);
}

static inline EventBits_t asic_task_notify_xn2_change(void) {
    return asic_task_event_set_bits(ASIC_TASK_EVENT_STRATUM_XN2_CHANGED);
}



static inline EventBits_t asic_task_notify_new_work(void) {
    return asic_task_event_set_bits(ASIC_TASK_EVENT_STRATUM_NEW_WORK);
}

static inline void asic_task_send_new_work(mining_notify* const newValue) {
    mining_notify* oldValue = atomic_exchange(&asic_task_mining_notify,newValue);
    if(oldValue != NULL) {
        STRATUM_V1_free_mining_notify(oldValue);
    }
    if(newValue != NULL) {
        asic_task_notify_new_work();
    }
}

/**
 * @brief Sends an ABANDON_WORK event and then waits for the WORK_ABANDONED event.
 * 
 * @param maxWait 
 * @return true 
 * @return false 
 */
static inline bool asic_task_abandon_work(const TickType_t maxWait) {
    asic_task_send_new_work(NULL);
    return 
        (
            xEventGroupSync(asic_task_event_handle,
                ASIC_TASK_EVENT_STRATUM_ABANDON_WORK,
                ASIC_TASK_EVENT_STRATUM_WORK_ABANDONED,
                maxWait)
            & ASIC_TASK_EVENT_STRATUM_WORK_ABANDONED
        ) 
        != 0;
}


#ifdef __cplusplus
}
#endif