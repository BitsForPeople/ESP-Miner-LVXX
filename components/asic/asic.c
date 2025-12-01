#include <string.h>
#include <assert.h>

#include <esp_log.h>
#include <sdkconfig.h>

#include "bm1397.h"
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"

#include "asic.h"
#include "device_config.h"
#include "frequency_transition_bmXX.h"

static const char* const TAG = "asic";

static const AsicDrvr_t* const DRIVERS[] = {
#if CONFIG_ASIC_BM1366_ENABLED
    &BM1366_drvr,
#endif
#if CONFIG_ASIC_BM1368_ENABLED
    &BM1368_drvr,
#endif
#if CONFIG_ASIC_BM1370_ENABLED
    &BM1370_drvr,
#endif
#if CONFIG_ASIC_BM1397_ENABLED
    &BM1397_drvr,
#endif
    // NULL
};

static_assert(sizeof(DRIVERS)/sizeof(DRIVERS[0]) >= 1, "Support for at least one ASIC must be enabled.");

static const AsicDrvr_t* ASIC_get_driver(const uint16_t chipId) {
    const AsicDrvr_t* bestDrvr = NULL;
    unsigned bestCompat = 0;

    for(unsigned i = 0; i < (sizeof(DRIVERS)/sizeof(DRIVERS[0])); ++i) {
        const unsigned compat = DRIVERS[i]->get_compatibility(chipId);
        if(compat > bestCompat) {
            bestCompat = compat;
            bestDrvr = DRIVERS[i];
        }
    }

    return bestDrvr;

}

uint8_t ASIC_init(GlobalState* const GLOBAL_STATE)
{

    ESP_LOGI(TAG, "Detecting ASICs...");

    uint16_t chipId = 0;
    const int cnt = ASIC_detect(&chipId);
    if(cnt <= 0) {
        // TODO: If cnt < 0 we may want to continue with -cnt ASICs as a fallback.
        ESP_LOGE(TAG, "Failed to detect ASIC(s) @ %d",1-cnt);
        return 0;
    } else {
        ESP_LOGI(TAG, "Detected ASIC %" PRIx16 " (x%d)", chipId, cnt); 

        GLOBAL_STATE->DEVICE_CONFIG.family.asic_count = cnt;

        const AsicDrvr_t* const drvr = ASIC_get_driver(chipId);
        GLOBAL_STATE->asic_drvr = drvr;
        if(drvr) {
            if(drvr->name) {
                ESP_LOGI(TAG, "Using ASIC driver \"%s\".",drvr->name);
            } else {
                ESP_LOGI(TAG, "ASIC driver found.");
            }
            return drvr->init(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value, cnt, GLOBAL_STATE->DEVICE_CONFIG.family.asic.difficulty);
        } else {
            ESP_LOGE(TAG, "No suitable driver found for ASIC %" PRIx16, chipId);
            return 0;
        }
    }
}

bool ASIC_set_frequency(GlobalState * GLOBAL_STATE, float frequency)
{
    do_frequency_transition(frequency,GLOBAL_STATE->asic_drvr->send_frequency);
    return true;
}
