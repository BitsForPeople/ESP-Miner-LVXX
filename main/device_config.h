#ifndef DEVICE_CONFIG_H_
#define DEVICE_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define THERMAL_MAX_SENSORS 2

typedef enum
{
    BM1397,
    BM1366,
    BM1368,
    BM1370,
} Asic;

typedef struct {
    Asic id;
    const char * name;
    uint16_t chip_id;
    uint16_t default_frequency_mhz;
    const uint16_t* frequency_options;
    uint16_t default_voltage_mv;
    const uint16_t* voltage_options;
    uint16_t hashrate_target;
    uint16_t difficulty;
    uint16_t core_count;
    uint16_t small_core_count;
    // test values
    float hashrate_test_percentage_target;
} AsicConfig;

typedef enum
{
    MAX,
    ULTRA,
    HEX,
    SUPRA,
    GAMMA,
    GAMMA_TURBO,
    LV06,
    LV07,
    LV08
} Family;

typedef struct {
    Family id;
    const char * name;
    AsicConfig asic;
    uint8_t asic_count;
    uint16_t max_power;
    uint16_t power_offset;
    uint16_t nominal_voltage;
    const char * swarm_color;
} FamilyConfig;

typedef struct {
    const char * board_version;
    FamilyConfig family;
    bool plug_sense;
    bool asic_enable;
    bool EMC2101 : 1;
    bool EMC2103 : 1;
    bool EMC2302 : 1;
    bool emc_internal_temp : 1;
    uint8_t emc_ideality_factor;
    uint8_t emc_beta_compensation;
    int8_t emc_temp_offset;
    bool DS4432U  : 1;
    bool INA260   : 1;
    bool TPS546   : 1;
    bool TPS546_1 : 1;
    bool TPS546_3 : 1;
    bool TMP1075  : 1;
    // test values
    uint16_t power_consumption_target;
} DeviceConfig;

esp_err_t device_config_init(void * pvParameters);

#endif /* DEVICE_CONFIG_H_ */
