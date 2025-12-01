#include "system.h"
#include <math.h>
#include "work_queue.h"
#include "serial.h"
#include <string.h>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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


void ASIC_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    //initialize the semaphore
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    GLOBAL_STATE->ASIC_TASK_MODULE.semaphore = sem;

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs = malloc(sizeof(bm_job *) * 128);
    GLOBAL_STATE->valid_jobs = malloc(sizeof(uint8_t) * 128);
    for (int i = 0; i < 128; i++)
    {
        GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[i] = NULL;
        GLOBAL_STATE->valid_jobs[i] = 0;
    }

    double asic_job_frequency_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);



    ESP_LOGI(TAG, "ASIC Job Interval: %.2f ms", asic_job_frequency_ms);
    SYSTEM_notify_mining_started(GLOBAL_STATE);
    ESP_LOGI(TAG, "ASIC Ready!");

    const TickType_t job_freq_ticks = (uint32_t)asic_job_frequency_ms / portTICK_PERIOD_MS;

    while (1)
    {
        bm_job *next_bm_job = (bm_job *)queue_dequeue(&GLOBAL_STATE->ASIC_jobs_queue);



        //(*GLOBAL_STATE->ASIC_functions.send_work_fn)(GLOBAL_STATE, next_bm_job); // send the job to the ASIC
        ASIC_send_work(GLOBAL_STATE, next_bm_job);

        // Time to execute the above code is ~0.3ms
        // Delay for ASIC(s) to finish the job
        //vTaskDelay((asic_job_frequency_ms - 0.3) / portTICK_PERIOD_MS);
        // xSemaphoreTake(GLOBAL_STATE->ASIC_TASK_MODULE.semaphore, asic_job_frequency_ms / portTICK_PERIOD_MS);
        xSemaphoreTake(sem, job_freq_ticks );
        // xSemaphoreTake(sem, getCurrentJobTimeTicks(GLOBAL_STATE));
    }
}
