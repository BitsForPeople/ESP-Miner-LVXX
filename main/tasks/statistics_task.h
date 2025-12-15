#ifndef STATISTICS_TASK_H_
#define STATISTICS_TASK_H_

#include "global_state.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct StatisticsData * StatisticsNodePtr;
typedef struct StatisticsData * StatisticsNextNodePtr;

struct StatisticsData
{
    // Members orderered by size (alignment) to minimize padding.
    // int64_t timestamp;
    uint32_t timestamp; // Resolution: 100ms
    // double hashrate;
    uint32_t hashrate_MHz;
    StatisticsNextNodePtr next;
    uint32_t freeHeap;
    float chipTemperature;
    float vrTemperature;
    float power;
    float voltage;
    float current;
    int16_t coreVoltageActual;
    uint16_t fanSpeed;
    uint16_t fanRPM;
    int8_t wifiRSSI;
};

// typedef struct
// {
//     StatisticsNodePtr * statisticsList;
// } StatisticsModule;

// StatisticsNodePtr addStatisticData(StatisticsNodePtr data);
// StatisticsNextNodePtr statisticData(StatisticsNodePtr nodeIn, StatisticsNodePtr dataOut);

// void clearStatisticData();

void statistics_init(void);
void statistics_deinit(void);

// void statistics_task(void* pvParameters);

/**
 * @brief Sets the interval for statistics collection.
 * Setting an interval of \c 0 seconds disables the data collection.
 * 
 * @param intervalSeconds 
 * @return true 
 * @return false 
 */
bool statistics_set_collection_interval(const uint16_t intervalSeconds);

bool statisticDataNext(StatisticsNodePtr prevNode, StatisticsNodePtr dataOut);


#ifdef __cplusplus
}
#endif


#endif // STATISTICS_TASK_H_
