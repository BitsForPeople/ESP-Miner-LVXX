#include "system.h"
#include <math.h>
#include <pthread.h>
#include "serial.h"
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "asic_task_intf.h"
#include "bm_job_builder.h"
#include "bm_job_pool.h"

#include "asic.h"

static const char* const TAG = "asic_task";

static inline float getHashrate_MHz(const GlobalState* const GLOBAL_STATE) {
    return (GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value * ASIC_get_hashes_per_clock(GLOBAL_STATE));
}

static inline uint32_t getMaxMsPerJob(const GlobalState* const GLOBAL_STATE, uint32_t version_mask) {
    // Divide by 1<<32 then divide by 1<<(version_mask popcount), i.e.
    // shift right by 32, then again by popcount.
    // Divide raw hashrate by (2^32 * 2^popcnt(version_mask))

    int rshift = 32;
    if(ASIC_has_version_rolling(GLOBAL_STATE)) {
        rshift += __builtin_popcount(GLOBAL_STATE->version_mask);
    }
    const float raw = getHashrate_MHz(GLOBAL_STATE);
    const float mhz = ldexpf(raw, -rshift); // negative shift amount needed for a *right* shift!
    const uint32_t msPerJob = 1.f/(mhz*1000.f);
    ESP_LOGW(TAG, "vmask: %" PRIx32 " hr: %" PRIu32 " -> max. %" PRIu32 "ms per job",
        GLOBAL_STATE->version_mask,
        (uint32_t)raw,
        msPerJob
    );
    return msPerJob;
}

typedef struct JobTime {
    float freq;
    uint32_t vmask;
    uint32_t ticks_per_job;
} JobTime_t;

static JobTime_t jobTime = {0};

static inline uint32_t getCurrentJobTimeTicks(const GlobalState* const GLOBAL_STATE) {
    if (GLOBAL_STATE->version_mask != jobTime.vmask ||
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value != jobTime.freq ||
        jobTime.ticks_per_job == 0) {

            jobTime.freq = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value;
            jobTime.vmask = GLOBAL_STATE->version_mask;
            uint32_t ms = getMaxMsPerJob(GLOBAL_STATE,GLOBAL_STATE->version_mask);

            ms = (ms < 10) ? 10 : ms;
            ms = (ms > 5000) ? 5000 : ms;
           
            jobTime.ticks_per_job = ms / portTICK_PERIOD_MS;
            ESP_LOGI(TAG, "Using %" PRIu32 "ms for job time.", ms);
    }
    return jobTime.ticks_per_job;
}

static TickType_t get_ticks_left(const TickType_t tStart, const TickType_t max_wait) {
    if(max_wait == portMAX_DELAY) {
        return max_wait;
    } else {
        const TickType_t elapsed = xTaskGetTickCount() - tStart;
        if(max_wait > elapsed) {
            return max_wait - elapsed;
        } else {
            return 0;
        }
    }
}



static const EventBits_t EVENTS = ASIC_TASK_EVENT_STRATUM_EVENTS;

static inline EventBits_t event_wait(TickType_t maxWait) {
    return xEventGroupWaitBits(asic_task_event_handle,EVENTS,true,false,maxWait);
}

static inline bool event_is_abandon_work(const EventBits_t bits) {
    return (bits & ASIC_TASK_EVENT_STRATUM_ABANDON_WORK) != 0;
}

static inline EventBits_t ack_abandon_work(void) {
    return xEventGroupSetBits( asic_task_event_handle, ASIC_TASK_EVENT_STRATUM_WORK_ABANDONED);
}

static inline bool event_is_new_work(EventBits_t bits) {
    return (bits & ASIC_TASK_EVENT_STRATUM_NEW_WORK) != 0;
}

static inline bool event_is_diff_change(EventBits_t bits) {
    return (bits & ASIC_TASK_EVENT_POOL_DIFF_CHANGED) != 0;
}

static inline bool event_is_version_change(EventBits_t bits) {
    return (bits & ASIC_TASK_EVENT_VERSION_MASK_CHANGED) != 0;
}

static inline mining_notify* take_work(void) {
    return atomic_exchange(&asic_task_mining_notify,NULL);
}




static inline void release_mining_notify(mining_notify* const mining_notification) {
    if(mining_notification != NULL) {
        STRATUM_V1_free_mining_notify(mining_notification);
    }
}

static inline void invalidate_all_jobs(GlobalState* const GLOBAL_STATE) {
    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    {
        for (int i = 0; i < 128; i = i + 4) {
            GLOBAL_STATE->valid_jobs[i] = 0;
        }
    }
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);
}



void ASIC_task(void *pvParameters)
{
    GlobalState* const GLOBAL_STATE = (GlobalState *)pvParameters;

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs = malloc(sizeof(bm_job *) * 128);
    GLOBAL_STATE->valid_jobs = malloc(sizeof(uint8_t) * 128);

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    for (int i = 0; i < 128; i++)
    {
        GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[i] = NULL;
        GLOBAL_STATE->valid_jobs[i] = 0;
    }
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

    double asic_job_frequency_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);

    ESP_LOGI(TAG, "ASIC Job Interval: %.2f ms", asic_job_frequency_ms);
    SYSTEM_notify_mining_started(GLOBAL_STATE);
    ESP_LOGI(TAG, "ASIC Ready!");

    const TickType_t job_freq_ticks = (uint32_t)asic_job_frequency_ms / portTICK_PERIOD_MS;

    TickType_t last_job_time = 0;

    mining_notify* mining_notification = NULL;
    uint64_t extranonce_2 = 0;

    uint32_t rnd = esp_random();
    while (1)
    {
        rnd += esp_random();
        /*
         * As long as we have valid mining_notify data, wake up (at least) at job_freq_ticks
         * intervals to build and send a new job to the ASIC.
         * If&while we have no (more) valid mining_notify data, we stop generating jobs and
         * only process events until we have mining_notify again.
         */
        const EventBits_t evt = event_wait(
            (mining_notification == NULL) ? 
                portMAX_DELAY
                :
                get_ticks_left(last_job_time,job_freq_ticks)
        );

        if(event_is_abandon_work(evt)) {
            // Acknowledge that we got the memo and are in the appropriate branch of execution now.
            ack_abandon_work();

            ESP_LOGI(TAG, "Abandoning work.");

            // Discontinue working with this notification.
            release_mining_notify(mining_notification);
            mining_notification = NULL;

            invalidate_all_jobs(GLOBAL_STATE);

            // The next job should start immediately when we get new work. So pretend the last job was sent a full interval ago.
            last_job_time = xTaskGetTickCount() - job_freq_ticks;
        }
        if(event_is_version_change(evt)) {
            ESP_LOGI(TAG, "New version mask %" PRIx32, (uint32_t)(GLOBAL_STATE->version_mask >> 13));
            ASIC_set_version_mask(GLOBAL_STATE, GLOBAL_STATE->version_mask);
        }
        if(event_is_diff_change(evt)) {
            // Ok...
        }
        if(event_is_new_work(evt)) {
            ESP_LOGI(TAG, "Getting new work.");

            // Discontinue working with this notification.
            release_mining_notify(mining_notification);
            mining_notification = NULL;
            
            extranonce_2 = rnd;
            mining_notification = take_work();
        }

        if(mining_notification != NULL) {
            if(get_ticks_left(last_job_time,job_freq_ticks) == 0) {
                // It's time to send a new job.
                last_job_time = xTaskGetTickCount();

                bm_job* const next_bm_job = bmjobpool_take();

                if(next_bm_job != NULL) {
                    if(bm_job_build(GLOBAL_STATE,mining_notification,extranonce_2, GLOBAL_STATE->pool_difficulty,next_bm_job)) {
                        extranonce_2 += 1;
                        ASIC_send_work(GLOBAL_STATE, next_bm_job);
                    } else {
                        ESP_LOGW(TAG, "bm_job_build failed.");
                    }
                } else {
                    ESP_LOGW(TAG, "Couldn't get a bm_job from the pool.");
                }
            }
        }
    }
}
