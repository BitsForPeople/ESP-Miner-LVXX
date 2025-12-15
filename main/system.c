#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

#include "system.h"
#include "i2c_bitaxe.h"
#include "INA260.h"
#include "adc.h"
#include "connect.h"
#include "nvs_config.h"
#include "display.h"
#include "input.h"
#include "screen.h"
#include "vcore.h"
#include "thermal.h"

static const char * const TAG = "system";

static void _suffix_string(uint64_t, char *, size_t, int);

//local function prototypes
static esp_err_t ensure_overheat_mode_config();

static void _check_for_best_diff(double diff, uint8_t job_id);
static void _suffix_string(uint64_t val, char * buf, size_t bufsiz, int sigdigits);

void SYSTEM_init_system(void)
{
    SystemModule * module = &GLOBAL_STATE.SYSTEM_MODULE;

    module->duration_start = 0;
    module->historical_hashrate_rolling_index = 0;
    module->historical_hashrate_init = 0;
    module->current_hashrate = 0;
    module->screen_page = 0;
    module->shares_accepted = 0;
    module->shares_rejected = 0;
    module->best_nonce_diff = nvs_config_get_u64(NVS_CONFIG_BEST_DIFF, 0);
    module->best_session_nonce_diff = 0;
    module->start_time = esp_timer_get_time();
    module->lastClockSync = 0;
    module->FOUND_BLOCK = false;
    
    // set the pool url
    module->pool_url = nvs_config_get_string(NVS_CONFIG_STRATUM_URL, CONFIG_STRATUM_URL);
    module->fallback_pool_url = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL, CONFIG_FALLBACK_STRATUM_URL);

    // set the pool port
    module->pool_port = nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT, CONFIG_STRATUM_PORT);
    module->fallback_pool_port = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, CONFIG_FALLBACK_STRATUM_PORT);

    // set the pool user
    module->pool_user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER, CONFIG_STRATUM_USER);
    module->fallback_pool_user = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER, CONFIG_FALLBACK_STRATUM_USER);

    // set the pool password
    module->pool_pass = nvs_config_get_string(NVS_CONFIG_STRATUM_PASS, CONFIG_STRATUM_PW);
    module->fallback_pool_pass = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_PASS, CONFIG_FALLBACK_STRATUM_PW);

    // set the pool difficulty
    module->pool_difficulty = nvs_config_get_u16(NVS_CONFIG_STRATUM_DIFFICULTY, CONFIG_STRATUM_DIFFICULTY);
    module->fallback_pool_difficulty = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY, CONFIG_FALLBACK_STRATUM_DIFFICULTY);

    // set the pool extranonce subscribe
    module->pool_extranonce_subscribe = nvs_config_get_u16(NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE, STRATUM_EXTRANONCE_SUBSCRIBE);
    module->fallback_pool_extranonce_subscribe = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE, FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE);

    // set fallback to false.
    module->is_using_fallback = false;

    // Initialize overheat_mode
    module->overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
    ESP_LOGI(TAG, "Initial overheat_mode value: %d", module->overheat_mode);

    //Initialize power_fault fault mode
    module->power_fault = 0;

    // set the best diff string
    _suffix_string(module->best_nonce_diff, module->best_diff_string, DIFF_STRING_SIZE, 0);
    _suffix_string(module->best_session_nonce_diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);
}

static const gpio_config_t RST_PIN_CFG = {
    .pin_bit_mask = (1ull << CONFIG_GPIO_ASIC_RESET),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
};

esp_err_t SYSTEM_init_ctrl_pins(void) {
    gpio_set_level(CONFIG_GPIO_ASIC_RESET,1);
    esp_err_t r = gpio_config(&RST_PIN_CFG);
    ESP_RETURN_ON_ERROR(r, TAG, "Error setting up RST pin.");
    // For now, the EN pin is set up by vcore module itself.
    return r;
}

esp_err_t SYSTEM_init_peripherals(void) {
    
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "Error installing ISR service");

    SYSTEM_init_ctrl_pins();

    // Initialize the core voltage regulator
    ESP_RETURN_ON_ERROR(VCORE_init(&GLOBAL_STATE), TAG, "VCORE init failed!");
    ESP_RETURN_ON_ERROR(VCORE_set_voltage(&GLOBAL_STATE, nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE) * 0.001f), TAG, "VCORE set voltage failed!");

    ESP_RETURN_ON_ERROR(Thermal_init(&GLOBAL_STATE.DEVICE_CONFIG), TAG, "Thermal init failed!");

    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Ensure overheat_mode config exists
    ESP_RETURN_ON_ERROR(ensure_overheat_mode_config(), TAG, "Failed to ensure overheat_mode config");

    ESP_RETURN_ON_ERROR(display_init(), TAG, "Display init failed!");

    ESP_RETURN_ON_ERROR(input_init(screen_next, toggle_wifi_softap), TAG, "Input init failed!");

    ESP_RETURN_ON_ERROR(screen_start(), TAG, "Screen start failed!");

    return ESP_OK;
}

void SYSTEM_notify_accepted_share(void)
{
    SystemModule* const module = &GLOBAL_STATE.SYSTEM_MODULE;

    module->shares_accepted++;
}

static int compare_rejected_reason_stats(const void *a, const void *b) {
    const RejectedReasonStat *ea = a;
    const RejectedReasonStat *eb = b;
    return (eb->count > ea->count) - (ea->count > eb->count);
}

void SYSTEM_notify_rejected_share(char * error_msg)
{
    SystemModule* const module = &GLOBAL_STATE.SYSTEM_MODULE;

    module->shares_rejected++;

    for (int i = 0; i < module->rejected_reason_stats_count; i++) {
        if (strncmp(module->rejected_reason_stats[i].message, error_msg, sizeof(module->rejected_reason_stats[i].message) - 1) == 0) {
            module->rejected_reason_stats[i].count++;
            return;
        }
    }

    if (module->rejected_reason_stats_count < sizeof(module->rejected_reason_stats)) {
        strncpy(module->rejected_reason_stats[module->rejected_reason_stats_count].message, 
                error_msg, 
                sizeof(module->rejected_reason_stats[module->rejected_reason_stats_count].message) - 1);
        module->rejected_reason_stats[module->rejected_reason_stats_count].message[sizeof(module->rejected_reason_stats[module->rejected_reason_stats_count].message) - 1] = '\0'; // Ensure null termination
        module->rejected_reason_stats[module->rejected_reason_stats_count].count = 1;
        module->rejected_reason_stats_count++;
    }

    if (module->rejected_reason_stats_count > 1) {
        qsort(module->rejected_reason_stats, module->rejected_reason_stats_count, 
            sizeof(module->rejected_reason_stats[0]), compare_rejected_reason_stats);
    }    
}

void SYSTEM_notify_mining_started(void)
{
    SystemModule * const module = &GLOBAL_STATE.SYSTEM_MODULE;

    module->duration_start = esp_timer_get_time();
}

void SYSTEM_notify_new_ntime(uint32_t ntime)
{
    SystemModule* const module = &GLOBAL_STATE.SYSTEM_MODULE;

    // Hourly clock sync
    if (module->lastClockSync + (60 * 60) > ntime) {
        return;
    }
    ESP_LOGI(TAG, "Syncing clock");
    module->lastClockSync = ntime;
    struct timeval tv;
    tv.tv_sec = ntime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
}

void SYSTEM_notify_found_nonce(double found_diff, uint8_t job_id)
{
    SystemModule* const module = &GLOBAL_STATE.SYSTEM_MODULE;

    // Calculate the time difference in seconds with sub-second precision
    // hashrate = (nonce_difficulty * 2^32) / time_to_find

    module->historical_hashrate[module->historical_hashrate_rolling_index] = GLOBAL_STATE.DEVICE_CONFIG.family.asic.difficulty;
    module->historical_hashrate_time_stamps[module->historical_hashrate_rolling_index] = esp_timer_get_time();

    module->historical_hashrate_rolling_index = (module->historical_hashrate_rolling_index + 1) % HISTORY_LENGTH;

    // ESP_LOGI(TAG, "nonce_diff %.1f, ttf %.1f, res %.1f", nonce_diff, duration,
    // historical_hashrate[historical_hashrate_rolling_index]);

    if (module->historical_hashrate_init < HISTORY_LENGTH) {
        module->historical_hashrate_init++;
    } else {
        module->duration_start =
            module->historical_hashrate_time_stamps[(module->historical_hashrate_rolling_index + 1) % HISTORY_LENGTH];
    }
    double sum = 0;
    for (int i = 0; i < module->historical_hashrate_init; i++) {
        sum += module->historical_hashrate[i];
    }

    double duration = (double) (esp_timer_get_time() - module->duration_start) / 1000000;

    double rolling_rate = (sum * 4294967296) / (duration * 1000000000);
    if (module->historical_hashrate_init < HISTORY_LENGTH) {
        module->current_hashrate = rolling_rate;
    } else {
        // More smoothing
        module->current_hashrate = ((module->current_hashrate * 9) + rolling_rate) / 10;
    }


    // logArrayContents(historical_hashrate, HISTORY_LENGTH);
    // logArrayContents(historical_hashrate_time_stamps, HISTORY_LENGTH);

    _check_for_best_diff(found_diff, job_id);
}

static double _calculate_network_difficulty(uint32_t nBits)
{
    static const double D1 = ldexp(65535,208);

    uint32_t mantissa = nBits & 0x007fffff;  // Extract the mantissa from nBits
    int exponent = (nBits >> 24) & 0xff; // Extract the exponent from nBits

    double target = ldexp((double)mantissa,8*(exponent-3)); // target = mantissa * 256^(exponent-3)

    // double target = (double) mantissa * pow(256, (exponent - 3)); // Calculate the target value

    // double difficulty = (pow(2, 208) * 65535) / target; // Calculate the difficulty

    // return difficulty;
    return D1 / target;
}

static void _check_for_best_diff(double diff, uint8_t job_id)
{
    SystemModule* const module = &GLOBAL_STATE.SYSTEM_MODULE;

    if ((uint64_t) diff > module->best_session_nonce_diff) {
        module->best_session_nonce_diff = (uint64_t) diff;
        _suffix_string((uint64_t) diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);
    }

    double network_diff = _calculate_network_difficulty(GLOBAL_STATE.ASIC_TASK_MODULE.active_jobs[job_id]->target);
    if (diff > network_diff) {
        module->FOUND_BLOCK = true;
        ESP_LOGI(TAG, "FOUND BLOCK!!!!!!!!!!!!!!!!!!!!!! %f > %f", diff, network_diff);
    }

    if ((uint64_t) diff <= module->best_nonce_diff) {
        return;
    }
    module->best_nonce_diff = (uint64_t) diff;

    nvs_config_set_u64(NVS_CONFIG_BEST_DIFF, module->best_nonce_diff);

    // make the best_nonce_diff into a string
    _suffix_string((uint64_t) diff, module->best_diff_string, DIFF_STRING_SIZE, 0);

    ESP_LOGI(TAG, "Network diff: %f", network_diff);
}

/* Convert a uint64_t value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
static void _suffix_string(uint64_t val, char * buf, size_t bufsiz, int sigdigits)
{
    const double dkilo = 1000.0;
    const uint64_t kilo = 1000ull;
    const uint64_t mega = 1000000ull;
    const uint64_t giga = 1000000000ull;
    const uint64_t tera = 1000000000000ull;
    const uint64_t peta = 1000000000000000ull;
    const uint64_t exa = 1000000000000000000ull;
    char suffix[2] = {'\0','\0'};
    bool decimal = true;
    double dval;

    if (val >= exa) {
        val /= peta;
        dval = (double) val / dkilo;
        // strcpy(suffix, "E");
        suffix[0] = 'E';
    } else if (val >= peta) {
        val /= tera;
        dval = (double) val / dkilo;
        // strcpy(suffix, "P");
        suffix[0] = 'P';
    } else if (val >= tera) {
        val /= giga;
        dval = (double) val / dkilo;
        // strcpy(suffix, "T");
        suffix[0] = 'T';
    } else if (val >= giga) {
        val /= mega;
        dval = (double) val / dkilo;
        // strcpy(suffix, "G");
        suffix[0] = 'G';
    } else if (val >= mega) {
        val /= kilo;
        dval = (double) val / dkilo;
        // strcpy(suffix, "M");
        suffix[0] = 'M';
    } else if (val >= kilo) {
        dval = (double) val / dkilo;
        // strcpy(suffix, "k");
        suffix[0] = 'k';
    } else {
        dval = val;
        decimal = false;
    }

    if (!sigdigits) {
        if (decimal)
            snprintf(buf, bufsiz, "%.2f %s", dval, suffix);
        else
            snprintf(buf, bufsiz, "%d %s", (unsigned int) dval, suffix);
    } else {
        /* Always show sigdigits + 1, padded on right with zeroes
         * followed by suffix */
        int ndigits = sigdigits - 1 - (dval > 0.0 ? floor(log10(dval)) : 0);

        snprintf(buf, bufsiz, "%*.*f %s", sigdigits + 1, ndigits, dval, suffix);
    }
}

static esp_err_t ensure_overheat_mode_config() {
    uint16_t overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, UINT16_MAX);

    if (overheat_mode == UINT16_MAX) {
        // Key doesn't exist or couldn't be read, set the default value
        nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
        ESP_LOGI(TAG, "Default value for overheat_mode set to 0");
    } else {
        // Key exists, log the current value
        ESP_LOGI(TAG, "Existing overheat_mode value: %d", overheat_mode);
    }

    return ESP_OK;
}
