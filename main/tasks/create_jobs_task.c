#include <sys/time.h>
#include <limits.h>

// #include "work_queue.h"
#include "ptrqueue.h"

#include "global_state.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mining.h"
#include "string.h"

#include "asic.h"
#include "mining_types.h"
#include "bm_job_pool.h"

static const char* const TAG = "create_jobs_task";

#define MAX_COINBASE_SIZE  256

static uint8_t coinbase_tx_buf[MAX_COINBASE_SIZE];

static const MemSpan_t COINBASE_BUF_SPAN = {
        .start = coinbase_tx_buf,
        .size = sizeof(coinbase_tx_buf)
    };


#define QUEUE_LOW_WATER_MARK 10 // Adjust based on your requirements

static bool should_generate_more_work(GlobalState *GLOBAL_STATE);
static void generate_work(GlobalState *GLOBAL_STATE, mining_notify *notification, uint64_t extranonce_2, uint32_t difficulty);

void create_jobs_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    if(bmjobpool_get_size() == 0) {
        // Expected number of active + queued jobs
        bmjobpool_grow_by((128/8)+QUEUE_LOW_WATER_MARK);
    }

    while (1)
    {
        asm volatile ("":"+m" (*GLOBAL_STATE));
        mining_notify *mining_notification = (mining_notify *)queue_dequeue(&GLOBAL_STATE->stratum_queue);

        if (mining_notification == NULL) {
            ESP_LOGE(TAG, "Failed to dequeue mining notification");
            vTaskDelay(100 / portTICK_PERIOD_MS); // Wait a bit before trying again
            continue;
        }

        ESP_LOGI(TAG, "New Work Dequeued %s", mining_notification->job_id);

        // uint32_t difficulty = GLOBAL_STATE->pool_difficulty;
        // difficulty = GLOBAL_STATE->pool_difficulty;

        if (GLOBAL_STATE->new_set_mining_difficulty_msg)
        {
            ESP_LOGI(TAG, "New pool difficulty %lu", GLOBAL_STATE->pool_difficulty);
            // difficulty = GLOBAL_STATE->pool_difficulty;
            GLOBAL_STATE->new_set_mining_difficulty_msg = false;
        }

        if (GLOBAL_STATE->new_stratum_version_rolling_msg) {
            ESP_LOGI(TAG, "Set chip version rolls %i", (int)(GLOBAL_STATE->version_mask >> 13));
            ASIC_set_version_mask(GLOBAL_STATE, GLOBAL_STATE->version_mask);
            GLOBAL_STATE->new_stratum_version_rolling_msg = false;
        }

        uint64_t extranonce_2 = 0;
        while (GLOBAL_STATE->stratum_queue.count < 1 && GLOBAL_STATE->abandon_work == 0)
        {
            if (should_generate_more_work(GLOBAL_STATE))
            {
                // difficulty = GLOBAL_STATE->pool_difficulty;
                generate_work(GLOBAL_STATE, mining_notification, extranonce_2, GLOBAL_STATE->pool_difficulty);

                // Increase extranonce_2 for the next job.
                extranonce_2++;
            }
            else
            {
                // If no more work needed, wait a bit before checking again.
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        }

        if (GLOBAL_STATE->abandon_work == 1)
        {
            GLOBAL_STATE->abandon_work = 0;

            queue_consume_all(&GLOBAL_STATE->ASIC_jobs_queue,&free_bm_job_from_queue);
            xSemaphoreGive(GLOBAL_STATE->ASIC_TASK_MODULE.semaphore);
        }

        STRATUM_V1_free_mining_notify(mining_notification);
    }
}

static bool should_generate_more_work(GlobalState *GLOBAL_STATE)
{
    return GLOBAL_STATE->ASIC_jobs_queue.count < QUEUE_LOW_WATER_MARK;
}

static void generate_work(GlobalState *GLOBAL_STATE, mining_notify *notification, uint64_t extranonce_2, uint32_t difficulty)
{
    char *extranonce_2_str = extranonce_2_generate(extranonce_2, GLOBAL_STATE->extranonce_2_len);
    if (extranonce_2_str == NULL) {
        ESP_LOGE(TAG, "Failed to generate extranonce_2");
        return;
    }
    //print generated extranonce_2
    //ESP_LOGI(TAG, "Generated extranonce_2: %s", extranonce_2_str);

    Hash_t merkle_root;
    {
        MemSpan_t cbtx = 
            construct_coinbase_tx_bin(
                notification->coinbase_1, notification->coinbase_2, GLOBAL_STATE->extranonce_str, extranonce_2_str,
                COINBASE_BUF_SPAN);

        if(cbtx.size == 0) {
            ESP_LOGE(TAG, "Failed to construct coinbase tx.");
            return;
        }
        calculate_merkle_root_hash_bin(cbtx,notification->merkle__branches,&merkle_root);
    }


    
    bm_job* const queued_next_job = bmjobpool_take();
    
    if(false) {
        bmjobpool_log_stats();
    }

    if (queued_next_job == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for queued_next_job");
        free(extranonce_2_str);
        return;
    }    
    construct_bm_job(notification, &merkle_root, GLOBAL_STATE->version_mask, difficulty, queued_next_job);

    queued_next_job->extranonce2 = extranonce_2_str; // Transfer ownership
    queued_next_job->jobid = strdup(notification->job_id);
    queued_next_job->version_mask = GLOBAL_STATE->version_mask;

    queue_enqueue(&GLOBAL_STATE->ASIC_jobs_queue, queued_next_job);

    // free(coinbase_tx);
    // free(merkle_root);
}