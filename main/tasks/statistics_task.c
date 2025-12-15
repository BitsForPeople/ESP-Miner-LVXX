#include <stdint.h>
#include <stdalign.h>
// #include <pthread.h>
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "statistics_task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "power.h"
#include "connect.h"
#include "vcore.h"

#include <esp_heap_caps.h>

#define DEFAULT_POLL_RATE 5000

static const char* const TAG = "statistics_task";

static StatisticsNodePtr statisticsDataStart = NULL;
static StatisticsNodePtr statisticsDataEnd = NULL;
// static pthread_mutex_t statisticsDataLock = PTHREAD_MUTEX_INITIALIZER;

static StaticSemaphore_t muxMem;
static SemaphoreHandle_t mux;

static void inline stats_lock(void) {
    if(mux) {
        xSemaphoreTake(mux,portMAX_DELAY);
    }
}

static void inline stats_unlock(void) {
    if(mux) {
        xSemaphoreGive(mux);
    }
}


static const uint16_t maxDataCount = 720;
static uint16_t currentDataCount;

static struct StatisticsData* statsBuffer;

static inline struct StatisticsData* allocStatNodes(const size_t cnt) {
    static const size_t ALIGN = alignof(struct StatisticsData);
    static const size_t SIZE = sizeof(struct StatisticsData);
    if(cnt == 0) {
        return NULL;
    } else {
        struct StatisticsData* mem;
        mem = heap_caps_aligned_alloc(ALIGN, cnt * SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if(!mem) {
            // ESP_LOGI(TAG, "Allocating stats buffer in internal RAM.");
            mem = heap_caps_aligned_alloc(ALIGN, cnt * SIZE, MALLOC_CAP_8BIT);
        }
        return mem;
    }
}

static inline struct StatisticsData* getStatsBuffer(void) {

    struct StatisticsData* mem = statsBuffer;

    {
        if(!mem) {
            currentDataCount = 0;
            mem = allocStatNodes(maxDataCount);
            statsBuffer = mem;
            if(!mem) {
                ESP_LOGE(TAG, "Failed to allocate stats buffer.");
            }
        }
    }

    return mem;
}

static inline void releaseStatsBuffer(void) {
    stats_lock();
    {
        if(statsBuffer != NULL) {
            ESP_LOGI(TAG, "Releasing stats buffer.");
            statisticsDataStart = NULL;
            statisticsDataEnd = NULL;
            currentDataCount = 0;

            free(statsBuffer);
            statsBuffer = NULL;
        }
    }
    stats_unlock();
}

static inline StatisticsNodePtr getNewNode(void) {
    if(currentDataCount < maxDataCount) {
        struct StatisticsData* const buffer = getStatsBuffer();
        if(buffer) {
            StatisticsNodePtr node = buffer + currentDataCount;
            currentDataCount += 1;
            return node;
        }
    }
    return NULL;
}

static StatisticsNodePtr addStatisticData(StatisticsNodePtr data)
{
    if (NULL == data) {
        return NULL;
    }

    StatisticsNodePtr newData = NULL;

    stats_lock();
    {
        if(currentDataCount < maxDataCount) {
            newData = getNewNode();
            if(!newData) {
                return NULL;
            }
            if(statisticsDataStart == NULL) {
                statisticsDataStart = newData;
                statisticsDataEnd = newData;
            }
        } else {
            newData = statisticsDataStart;
            statisticsDataStart = statisticsDataStart->next;
        }
        statisticsDataEnd->next = newData;
        statisticsDataEnd = newData;
        *newData = *data;
        newData->next = NULL;
    }
    stats_unlock();

    // create new data block or reuse first data block
    // if (currentDataCount < maxDataCount) {
    //     newData = allocStatNodes(1);
    //     // (StatisticsNodePtr)malloc(sizeof(struct StatisticsData));
    //     if(NULL != newData) {
    //         currentDataCount++;
    //     } else {
    //         ESP_LOGE(TAG, "Failed to allocate stats node.");
    //     }
    // } 
    // pthread_mutex_lock(&statisticsDataLock);
    // {
    //     if(newData == NULL) {
    //         newData = statisticsDataStart;
    //         if(statisticsDataStart != NULL) {
    //             statisticsDataStart = statisticsDataStart->next;
    //         }
    //     } 
    //     if(newData != NULL) {
    //         if(statisticsDataEnd != NULL /* && newData != statisticsDataEnd */) {
    //             statisticsDataEnd->next = newData;
    //         }
    //         *newData = *data;
    //         newData->next = NULL;
    //         statisticsDataEnd = newData;
    //     }

    // }
    // pthread_mutex_unlock(&statisticsDataLock);
    // set data
    // if (NULL != newData) {

    //     // *newData = *data;

    //     pthread_mutex_lock(&statisticsDataLock);

    //     if (NULL == statisticsDataStart) {
    //         statisticsDataStart = newData; // set first new data block
    //     } else {
    //         if ((statisticsDataStart == newData) && (NULL != statisticsDataStart->next)) {
    //             statisticsDataStart = statisticsDataStart->next; // move DataStart to next (first data block reused)
    //         }
    //     }


    //     newData->next = NULL;

    //     if ((NULL != statisticsDataEnd) && (newData != statisticsDataEnd)) {
    //         statisticsDataEnd->next = newData; // link data block
    //     }
    //     statisticsDataEnd = newData; // set DataEnd to new data

    //     pthread_mutex_unlock(&statisticsDataLock);
    // }

    return newData;
}

bool statisticDataNext(StatisticsNodePtr prevNode, StatisticsNodePtr dataOut)
{

    if(NULL == dataOut) {
        return false;
    }

    StatisticsNodePtr data = NULL;

    // pthread_mutex_lock(&statisticsDataLock);
    stats_lock();
    {

        if(NULL == prevNode) {
            data = statisticsDataStart;
        } else {
            data = prevNode->next;
        }

        if(NULL != data) {
            *dataOut = *data;
        }

    }
    // pthread_mutex_unlock(&statisticsDataLock);
    stats_unlock();

    return NULL != data;
}

void clearStatisticData()
{
    if (NULL != statisticsDataStart) {
        // pthread_mutex_lock(&statisticsDataLock);
        stats_lock();

        StatisticsNextNodePtr nextNode = statisticsDataStart;

        while (NULL != nextNode) {
            StatisticsNodePtr node = nextNode;
            nextNode = node->next;
            free(node);
        }

        statisticsDataStart = NULL;
        statisticsDataEnd = NULL;
        currentDataCount = 0;

        // pthread_mutex_unlock(&statisticsDataLock);
        stats_unlock();
    }
}



static TimerHandle_t statsTimerHdl = NULL;
static StaticTimer_t statsTimerMem;
static volatile bool statsTimerShutdown;

static void statsTimerCb(TimerHandle_t xTimer);

bool statistics_set_collection_interval(uint16_t intervalSeconds);

void statistics_init(void)
{
    if(mux == NULL) {
        mux = xSemaphoreCreateMutexStatic(&muxMem);
    }
    if(statsTimerHdl == NULL) {
        // GlobalState* const GLOBAL_STATE = (GlobalState *) pvParameters;
        statsTimerHdl = xTimerCreateStatic("stats",
            DEFAULT_POLL_RATE / portTICK_PERIOD_MS,
            true,
            NULL,
            statsTimerCb,
            &statsTimerMem);
    }

    statistics_set_collection_interval(
            nvs_config_get_u16(NVS_CONFIG_STATISTICS_FREQUENCY, 0)
        );    
}


void statistics_deinit(void) {
    if(statsTimerHdl != NULL) {
        statistics_set_collection_interval(0);        
    }
}

static inline void collectStats(void) {
    SystemModule* const sys_module = &GLOBAL_STATE.SYSTEM_MODULE;
    PowerManagementModule* const power_management = &GLOBAL_STATE.POWER_MANAGEMENT_MODULE;
    struct StatisticsData statsData = {};

    const uint32_t currentTime = esp_timer_get_time() / (1000*100); // Resolution: 100ms

    int8_t wifiRSSI = -90;
    get_wifi_current_rssi(&wifiRSSI);

    statsData.timestamp = currentTime;
    statsData.hashrate_MHz = sys_module->current_hashrate * 1000.f;
    statsData.chipTemperature = power_management->chip_temp_avg;
    statsData.vrTemperature = power_management->vr_temp;
    statsData.power = power_management->power;
    statsData.voltage = power_management->voltage;
    statsData.current = Power_get_current(&GLOBAL_STATE);
    statsData.coreVoltageActual = VCORE_get_voltage_mv(&GLOBAL_STATE);
    statsData.fanSpeed = power_management->fan_perc;
    statsData.fanRPM = power_management->fan_rpm;
    statsData.wifiRSSI = wifiRSSI;
    statsData.freeHeap = esp_get_free_heap_size();

    addStatisticData(&statsData);

}

static void statsTimerCb(TimerHandle_t xTimer) {
    if(!statsTimerShutdown) {
        collectStats();
    } else {
        releaseStatsBuffer();
        /* Controlling a timer from the timer callback could lead to a deadlock,
           so we only wait a little while, and if not successful, we'll try again
           on the next timer cycle.
        */
        xTimerStop(statsTimerHdl, 10 / portTICK_PERIOD_MS);
    }
}


bool statistics_set_collection_interval(const uint16_t intervalSeconds) {
    if(statsTimerHdl != NULL) {
        if(intervalSeconds == 0) {
            // Ask the timer job to clean up and stop:
            statsTimerShutdown = true;
            ESP_LOGI(TAG, "Stats collection disabled.");
        } else {
            statsTimerShutdown = false;

            ESP_LOGI(TAG, "Collecting stats every %" PRIu16 "s.",intervalSeconds);

            xTimerChangePeriod(statsTimerHdl,
                ((uint32_t)intervalSeconds * 1000) / portTICK_PERIOD_MS,
                portMAX_DELAY); // This also starts the timer if it's not active yet.
        }
        return true;
    } else {
        ESP_LOGE(TAG, "Stats not initialized.");
        return false;
    }
}


// void statistics_task(void * pvParameters)
// {
//     ESP_LOGI(TAG, "Starting");

//     GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
//     SystemModule * sys_module = &GLOBAL_STATE.SYSTEM_MODULE;
//     PowerManagementModule * power_management = &GLOBAL_STATE.POWER_MANAGEMENT_MODULE;
//     struct StatisticsData statsData = {};

//     TickType_t taskWakeTime = xTaskGetTickCount();

//     while (1) {
//         const int64_t currentTime = esp_timer_get_time() / 1000;
//         statsFrequency = nvs_config_get_u16(NVS_CONFIG_STATISTICS_FREQUENCY, 0) * 1000;

//         if(statsFrequency != 0) {

//             const int64_t waitingTime = statsData.timestamp + statsFrequency - (DEFAULT_POLL_RATE / 2);

//             if (currentTime > waitingTime) {
//                 int8_t wifiRSSI = -90;
//                 get_wifi_current_rssi(&wifiRSSI);

//                 statsData.timestamp = currentTime;
//                 statsData.hashrate = sys_module->current_hashrate;
//                 statsData.chipTemperature = power_management->chip_temp_avg;
//                 statsData.vrTemperature = power_management->vr_temp;
//                 statsData.power = power_management->power;
//                 statsData.voltage = power_management->voltage;
//                 statsData.current = Power_get_current(GLOBAL_STATE);
//                 statsData.coreVoltageActual = VCORE_get_voltage_mv(GLOBAL_STATE);
//                 statsData.fanSpeed = power_management->fan_perc;
//                 statsData.fanRPM = power_management->fan_rpm;
//                 statsData.wifiRSSI = wifiRSSI;
//                 statsData.freeHeap = esp_get_free_heap_size();

//                 addStatisticData(&statsData);
//             }
//         } else {
//             // clearStatisticData();
//             releaseStatsBuffer();
//         }

//         vTaskDelayUntil(&taskWakeTime, DEFAULT_POLL_RATE / portTICK_PERIOD_MS); // taskWakeTime is automatically updated
//     }
// }
