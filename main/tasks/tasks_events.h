#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#ifdef __cplusplus
extern "C" {
#endif

// Using just one event group for all three tasks involved here.

// stratum -> create job
#define TASKS_EVENTS_POOL_DIFF_CHANGED      (1<<0)
#define TASKS_EVENTS_VERSION_MASK_CHANGED   (1<<1)

#define TASKS_EVENTS_STRATUM_NEW_WORK       (1<<2)
// // create job -> stratum
// #define TASKS_EVENTS_STRATUM_GOT_WORK       (1<<3) // response to NEW_WORK

#define TASKS_EVENTS_STRATUM_XN2_CHANGED    (1<<4)
// stratum -> create job
#define TASKS_EVENTS_STRATUM_ABANDON_WORK   (1<<5)
// create job -> stratum
#define TASKS_EVENTS_STRATUM_WORK_ABANDONED (1<<6) // response to ABANDON_WORK

// // create job -> asic
// #define TASKS_EVENTS_NEW_JOB                (1<<7)
// // asic -> create job
// #define TASKS_EVENTS_GOT_JOB                (1<<8) // response to NEW_JOB
// // create job -> asic
// #define TASKS_EVENTS_ABANDON_JOBS           (1<<9)
// // asic -> create job
// #define TASKS_EVENTS_JOBS_ABANDONED         (1<<10) // response to ABANDON_JOBS

#define TASKS_EVENTS_STRATUM_EVENTS         (\
    TASKS_EVENTS_POOL_DIFF_CHANGED | \
    TASKS_EVENTS_VERSION_MASK_CHANGED | \
    TASKS_EVENTS_STRATUM_NEW_WORK | \
    TASKS_EVENTS_STRATUM_ABANDON_WORK | \
    TASKS_EVENTS_STRATUM_XN2_CHANGED)

// #define TASKS_EVENTS_JOB_EVENTS             (TASKS_EVENTS_NEW_JOB | TASKS_EVENTS_ABANDON_JOBS)


extern EventGroupHandle_t tasks_events_handle;

void tasks_events_init(void);

static inline EventBits_t tasks_events_set_bits(const EventBits_t bits) {
    return xEventGroupSetBits(tasks_events_handle,bits);
}

static inline EventBits_t tasks_events_wait(const EventBits_t bitsToWaitFor, const bool clearOnExit, const TickType_t maxWait) {
    return xEventGroupWaitBits(tasks_events_handle,bitsToWaitFor,clearOnExit,0,maxWait);
}

static inline EventBits_t tasks_events_clear_bits(const EventBits_t bits) {
    return xEventGroupClearBits(tasks_events_handle,bits);
}

static inline EventBits_t tasks_events_get_bits(void) {
    return xEventGroupGetBits(tasks_events_handle);
}

static inline EventBits_t tasks_notify_diff_change(void) {
    return tasks_events_set_bits(TASKS_EVENTS_POOL_DIFF_CHANGED);
}

static inline EventBits_t tasks_notify_version_change(void) {
    return tasks_events_set_bits(TASKS_EVENTS_VERSION_MASK_CHANGED);
}

static inline EventBits_t tasks_notify_xn2_change(void) {
    return tasks_events_set_bits(TASKS_EVENTS_STRATUM_XN2_CHANGED);
}

static inline EventBits_t tasks_notify_abandon_work(void) {
    return tasks_events_set_bits(TASKS_EVENTS_STRATUM_ABANDON_WORK);
}

static inline bool tasks_sync_abandon_work(const TickType_t maxWait) {
    return 
    (xEventGroupSync(tasks_events_handle,
        TASKS_EVENTS_STRATUM_ABANDON_WORK,
        TASKS_EVENTS_STRATUM_WORK_ABANDONED,
        maxWait)
     & TASKS_EVENTS_STRATUM_WORK_ABANDONED) 
        != 0;
}

static inline EventBits_t tasks_notify_stratum_new_work(void) {
    return tasks_events_set_bits(TASKS_EVENTS_STRATUM_NEW_WORK);
}

static inline EventBits_t tasks_events_wait_stratum(const TickType_t maxWait) {
    return tasks_events_wait(TASKS_EVENTS_STRATUM_EVENTS, true, maxWait );
}


static inline bool event_is_stratum_abandon_work(const EventBits_t bits) {
    return (bits & TASKS_EVENTS_STRATUM_ABANDON_WORK) != 0;
}

static inline bool event_is_stratum_new_work(EventBits_t bits) {
    return (bits & TASKS_EVENTS_STRATUM_NEW_WORK) != 0;
}

static inline bool event_is_diff_change(EventBits_t bits) {
    return (bits & TASKS_EVENTS_POOL_DIFF_CHANGED) != 0;
}

static inline bool event_is_version_change(EventBits_t bits) {
    return (bits & TASKS_EVENTS_VERSION_MASK_CHANGED) != 0;
}





#ifdef __cplusplus
}
#endif