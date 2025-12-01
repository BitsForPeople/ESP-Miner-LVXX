#include <string.h>
#include "device_config.h"
#include "nvs_config.h"
#include "global_state.h"
#include "esp_log.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const char * const TAG = "device_config";


static const uint16_t BM1397_FREQUENCY_OPTIONS[] = {400, 425, 450, 475, 485, 500, 525, 550, 575, 600,                0};
static const uint16_t BM1366_FREQUENCY_OPTIONS[] = {300, 325, 350, 375, 400, 425, 450, 475, 485, 500, 525, 550, 575, 0};
static const uint16_t BM1368_FREQUENCY_OPTIONS[] = {400, 425, 450, 475, 485, 490, 500, 525, 550, 575,                0};
static const uint16_t BM1370_FREQUENCY_OPTIONS[] = {400, 490, 525, 550, 600, 625,                                    0};

static const uint16_t BM1397_VOLTAGE_OPTIONS[] = {1100, 1150, 1200, 1250, 1300, 1350, 1400, 1450, 1500, 0};
static const uint16_t BM1366_VOLTAGE_OPTIONS[] = {1000, 1050, 1100, 1150, 1200, 1250, 1300,             0};
static const uint16_t BM1368_VOLTAGE_OPTIONS[] = {1100, 1150, 1166, 1200, 1250, 1300,                   0};
static const uint16_t BM1370_VOLTAGE_OPTIONS[] = {1000, 1060, 1100, 1150, 1200, 1250,                   0};

static const AsicConfig ASIC_BM1397 = { .id = BM1397, .name = "BM1397", .chip_id = 1397, .default_frequency_mhz = 425, .frequency_options = BM1397_FREQUENCY_OPTIONS, .default_voltage_mv = 1400, .voltage_options = BM1397_VOLTAGE_OPTIONS, .difficulty = 256, .core_count = 168, .small_core_count =  672, .hashrate_test_percentage_target = 0.85, };
static const AsicConfig ASIC_BM1366 = { .id = BM1366, .name = "BM1366", .chip_id = 1366, .default_frequency_mhz = 485, .frequency_options = BM1366_FREQUENCY_OPTIONS, .default_voltage_mv = 1200, .voltage_options = BM1366_VOLTAGE_OPTIONS, .difficulty = 256, .core_count = 112, .small_core_count =  894, .hashrate_test_percentage_target = 0.85, };
static const AsicConfig ASIC_BM1368 = { .id = BM1368, .name = "BM1368", .chip_id = 1368, .default_frequency_mhz = 490, .frequency_options = BM1368_FREQUENCY_OPTIONS, .default_voltage_mv = 1166, .voltage_options = BM1368_VOLTAGE_OPTIONS, .difficulty = 256, .core_count =  80, .small_core_count = 1276, .hashrate_test_percentage_target = 0.80, };
static const AsicConfig ASIC_BM1370 = { .id = BM1370, .name = "BM1370", .chip_id = 1370, .default_frequency_mhz = 525, .frequency_options = BM1370_FREQUENCY_OPTIONS, .default_voltage_mv = 1150, .voltage_options = BM1370_VOLTAGE_OPTIONS, .difficulty = 256, .core_count = 128, .small_core_count = 2040, .hashrate_test_percentage_target = 0.85, };

static const AsicConfig default_asic_configs[] = {
    ASIC_BM1397,
    ASIC_BM1366,
    ASIC_BM1368,
    ASIC_BM1370,
};

static const FamilyConfig FAMILY_MAX         = { .id = MAX,         .name = "Max",        .asic = ASIC_BM1397, .asic_count = 1, .max_power = 25,  .power_offset = 5,  .nominal_voltage = 5,  .swarm_color = "red",    };
static const FamilyConfig FAMILY_ULTRA       = { .id = ULTRA,       .name = "Ultra",      .asic = ASIC_BM1366, .asic_count = 1, .max_power = 25,  .power_offset = 5,  .nominal_voltage = 5,  .swarm_color = "purple", };
static const FamilyConfig FAMILY_HEX         = { .id = HEX,         .name = "Hex",        .asic = ASIC_BM1366, .asic_count = 6, .max_power = 0,   .power_offset = 5,  .nominal_voltage = 5,  .swarm_color = "orange", };
static const FamilyConfig FAMILY_SUPRA       = { .id = SUPRA,       .name = "Supra",      .asic = ASIC_BM1368, .asic_count = 1, .max_power = 40,  .power_offset = 5,  .nominal_voltage = 5,  .swarm_color = "blue",   };
static const FamilyConfig FAMILY_GAMMA       = { .id = GAMMA,       .name = "Gamma",      .asic = ASIC_BM1370, .asic_count = 1, .max_power = 40,  .power_offset = 5,  .nominal_voltage = 5,  .swarm_color = "green",  };
static const FamilyConfig FAMILY_GAMMA_TURBO = { .id = GAMMA_TURBO, .name = "GammaTurbo", .asic = ASIC_BM1370, .asic_count = 2, .max_power = 60,  .power_offset = 10, .nominal_voltage = 12, .swarm_color = "cyan",   };
static const FamilyConfig FAMILY_LV06        = { .id = LV06,        .name = "LV06",       .asic = ASIC_BM1366, .asic_count = 2, .max_power = 40,  .power_offset = 6,  .nominal_voltage = 12, .swarm_color = "red", };
static const FamilyConfig FAMILY_LV07        = { .id = LV07,        .name = "LV07",       .asic = ASIC_BM1366, .asic_count = 2, .max_power = 40,  .power_offset = 6,  .nominal_voltage = 12, .swarm_color = "blue", };
static const FamilyConfig FAMILY_LV08        = { .id = LV08,        .name = "LV08",       .asic = ASIC_BM1366, .asic_count = 9, .max_power = 140, .power_offset = 18, .nominal_voltage = 12, .swarm_color = "green",   };

static const FamilyConfig default_families[] = {
    FAMILY_MAX,
    FAMILY_ULTRA,
    FAMILY_HEX,
    FAMILY_SUPRA,
    FAMILY_GAMMA,
    FAMILY_GAMMA_TURBO,
    FAMILY_LV06,
    FAMILY_LV07,
    FAMILY_LV08
};


static const DeviceConfig default_configs[] = {
    { .board_version = "2.2",  .family = FAMILY_MAX,         .plug_sense = true, .asic_enable = true, .EMC2101 = true,                                                                                     .DS4432U = true, .INA260 = true, .power_consumption_target = 12, },
    { .board_version = "102",  .family = FAMILY_MAX,         .plug_sense = true, .asic_enable = true, .EMC2101 = true,                                                                                     .DS4432U = true, .INA260 = true, .power_consumption_target = 12, },
    { .board_version = "0.11", .family = FAMILY_ULTRA,       .plug_sense = true, .asic_enable = true, .EMC2101 = true, .emc_internal_temp = true,                                  .emc_temp_offset = 5,   .DS4432U = true, .INA260 = true, .power_consumption_target = 12, },
    { .board_version = "201",  .family = FAMILY_ULTRA,       .plug_sense = true, .asic_enable = true, .EMC2101 = true, .emc_internal_temp = true,                                  .emc_temp_offset = 5,   .DS4432U = true, .INA260 = true, .power_consumption_target = 12, },
    { .board_version = "202",  .family = FAMILY_ULTRA,       .plug_sense = true, .asic_enable = true, .EMC2101 = true, .emc_internal_temp = true,                                  .emc_temp_offset = 5,   .DS4432U = true, .INA260 = true, .power_consumption_target = 12, },
    { .board_version = "203",  .family = FAMILY_ULTRA,       .plug_sense = true, .asic_enable = true, .EMC2101 = true, .emc_internal_temp = true,                                  .emc_temp_offset = 5,   .DS4432U = true, .INA260 = true, .power_consumption_target = 12, },
    { .board_version = "204",  .family = FAMILY_ULTRA,       .plug_sense = true,                      .EMC2101 = true, .emc_internal_temp = true,                                  .emc_temp_offset = 5,   .DS4432U = true, .INA260 = true, .power_consumption_target = 12, },
    { .board_version = "205",  .family = FAMILY_ULTRA,       .plug_sense = true, .asic_enable = true, .EMC2101 = true, .emc_internal_temp = true,                                  .emc_temp_offset = 5,   .DS4432U = true, .INA260 = true, .power_consumption_target = 12, },
    { .board_version = "207",  .family = FAMILY_ULTRA,       .EMC2101 = true,                                                                                     .TPS546 = true,                                                           .power_consumption_target = 12, },
    { .board_version = "400",  .family = FAMILY_SUPRA,       .plug_sense = true, .asic_enable = true, .EMC2101 = true, .emc_internal_temp = true,                                  .emc_temp_offset = 5,   .DS4432U = true, .INA260 = true, .power_consumption_target = 12, },
    { .board_version = "401",  .family = FAMILY_SUPRA,       .plug_sense = true, .asic_enable = true, .EMC2101 = true, .emc_internal_temp = true,                                  .emc_temp_offset = 5,   .DS4432U = true, .INA260 = true, .power_consumption_target = 12, },
    { .board_version = "402",  .family = FAMILY_SUPRA,       .EMC2101 = true,                                                                                     .TPS546 = true,                                                           .power_consumption_target = 8,  },
    { .board_version = "403",  .family = FAMILY_SUPRA,       .EMC2101 = true,                                                                                     .TPS546 = true,                                                           .power_consumption_target = 8,  },
    { .board_version = "600",  .family = FAMILY_GAMMA,       .EMC2101 = true, .emc_ideality_factor = 0x24, .emc_beta_compensation = 0x00,                         .TPS546 = true,                                                           .power_consumption_target = 19, },
    { .board_version = "601",  .family = FAMILY_GAMMA,       .EMC2101 = true, .emc_ideality_factor = 0x24, .emc_beta_compensation = 0x00,                         .TPS546 = true,                                                           .power_consumption_target = 19, },
    { .board_version = "602",  .family = FAMILY_GAMMA,       .EMC2101 = true, .emc_ideality_factor = 0x24, .emc_beta_compensation = 0x00,                         .TPS546 = true,                                                           .power_consumption_target = 22, },
    { .board_version = "800",  .family = FAMILY_GAMMA_TURBO, .EMC2103 = true,                                                             .emc_temp_offset = -10, .TPS546 = true,                                                           .power_consumption_target = 12, },
    { .board_version = "301_",  .family = FAMILY_LV06,        .EMC2302 = true,                                                             .emc_temp_offset = 5,   .TPS546_1 = true, .TMP1075 = true,                                        .power_consumption_target = 12, },
    { .board_version = "301",  .family = FAMILY_LV07,        .EMC2302 = true,                                                             .emc_temp_offset = 5,   .TPS546_1 = true, .TMP1075 = true,                                        .power_consumption_target = 12, },
    { .board_version = "302",  .family = FAMILY_LV08,        .EMC2302 = true,                                                             .emc_temp_offset = 5,   .TPS546_3 = true, .TMP1075 = true,                                        .power_consumption_target = 12, },
};


esp_err_t device_config_init(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    // TODO: Read board version from eFuse

    char * board_version = nvs_config_get_string(NVS_CONFIG_BOARD_VERSION, "000");

    for (int i = 0 ; i < ARRAY_SIZE(default_configs); i++) {
        if (strcmp(default_configs[i].board_version, board_version) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG = default_configs[i];

            ESP_LOGI(TAG, "Device Model: %s", GLOBAL_STATE->DEVICE_CONFIG.family.name);
            ESP_LOGI(TAG, "Board Version: %s", GLOBAL_STATE->DEVICE_CONFIG.board_version);
            ESP_LOGI(TAG, "ASIC: %dx %s (%d cores)", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);

            free(board_version);
            return ESP_OK;
        }
    }

    ESP_LOGI(TAG, "Custom Board Version: %s", board_version);

    GLOBAL_STATE->DEVICE_CONFIG.board_version = board_version; //  strdup(board_version);

    char * device_model = nvs_config_get_string(NVS_CONFIG_DEVICE_MODEL, "unknown");

    for (int i = 0 ; i < ARRAY_SIZE(default_families); i++) {
        if (strcasecmp(default_families[i].name, device_model) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG.family = default_families[i];

            ESP_LOGI(TAG, "Device Model: %s", GLOBAL_STATE->DEVICE_CONFIG.family.name);

            break;
        }
    }

    char * asic_model = nvs_config_get_string(NVS_CONFIG_ASIC_MODEL, "unknown");

    for (int i = 0 ; i < ARRAY_SIZE(default_asic_configs); i++) {
        if (strcasecmp(default_asic_configs[i].name, asic_model) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG.family.asic = default_asic_configs[i];

            ESP_LOGI(TAG, "ASIC: %dx %s (%d cores)", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);

            break;
        }
    }

    GLOBAL_STATE->DEVICE_CONFIG.plug_sense = nvs_config_get_u16(NVS_CONFIG_PLUG_SENSE, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.asic_enable = nvs_config_get_u16(NVS_CONFIG_ASIC_ENABLE, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.EMC2101 = nvs_config_get_u16(NVS_CONFIG_EMC2101, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.EMC2103 = nvs_config_get_u16(NVS_CONFIG_EMC2103, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.emc_internal_temp = nvs_config_get_u16(NVS_CONFIG_EMC_INTERNAL_TEMP, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.emc_ideality_factor = nvs_config_get_u16(NVS_CONFIG_EMC_IDEALITY_FACTOR, 0);
    GLOBAL_STATE->DEVICE_CONFIG.emc_beta_compensation = nvs_config_get_u16(NVS_CONFIG_EMC_BETA_COMPENSATION, 0);
    GLOBAL_STATE->DEVICE_CONFIG.emc_temp_offset = nvs_config_get_i32(NVS_CONFIG_EMC_TEMP_OFFSET, 0);
    GLOBAL_STATE->DEVICE_CONFIG.DS4432U = nvs_config_get_u16(NVS_CONFIG_DS4432U, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.INA260 = nvs_config_get_u16(NVS_CONFIG_INA260, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.TPS546 = nvs_config_get_u16(NVS_CONFIG_TPS546, 0) != 0;
    // test values
    GLOBAL_STATE->DEVICE_CONFIG.power_consumption_target = nvs_config_get_u16(NVS_CONFIG_POWER_CONSUMPTION_TARGET, 0);

    // free(board_version);
    free(device_model);
    free(asic_model);

    return ESP_OK;
}
