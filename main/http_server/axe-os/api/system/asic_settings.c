#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
// #include "cJSON.h"
#include "global_state.h"
#include "asic.h"
#include "http_json_writer.h"

// static const char *TAG = "asic_settings";
// static GlobalState *GLOBAL_STATE = NULL;

// Function declarations from http_server.c
extern esp_err_t is_network_allowed(httpd_req_t *req);
extern esp_err_t set_cors_headers(httpd_req_t *req);

// Initialize the ASIC API with the global state
// void asic_api_init(GlobalState *global_state) {
//     GLOBAL_STATE = global_state;
// }

static void sendOptions(http_writer_t* const w, const uint16_t* values) {
    const uint16_t* p = values;
    while(p[0] != 0) {
        http_json_write_value(w, p[0]);
        p += 1;
    }
}

/* Handler for system asic endpoint */
esp_err_t GET_system_asic(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    http_writer_t wrtr;
    http_writer_t* const w = &wrtr;
    http_writer_init(w,req);

    http_json_start_obj(w,NULL);

        http_json_write_item(w,"ASICModel", GLOBAL_STATE.DEVICE_CONFIG.family.asic.name);
        http_json_write_item(w,"deviceModel", GLOBAL_STATE.DEVICE_CONFIG.family.name);
        http_json_write_item(w,"swarmColor", GLOBAL_STATE.DEVICE_CONFIG.family.swarm_color);
        http_json_write_item(w,"asicCount", GLOBAL_STATE.DEVICE_CONFIG.family.asic_count);
        http_json_write_item(w,"defaultFrequency", GLOBAL_STATE.DEVICE_CONFIG.family.asic.default_frequency_mhz);
        http_json_write_item(w,"ASICModel", GLOBAL_STATE.DEVICE_CONFIG.family.asic.name);

        http_json_start_arr(w,"frequencyOptions");
            sendOptions(w,GLOBAL_STATE.DEVICE_CONFIG.family.asic.frequency_options);
        http_json_end_arr(w);

        http_json_start_arr(w,"voltageOptions");
            sendOptions(w, GLOBAL_STATE.DEVICE_CONFIG.family.asic.voltage_options);
        http_json_end_arr(w);

    http_json_end_obj(w);
    http_writer_finish(w);

    // cJSON *root = cJSON_CreateObject();

    // // Add ASIC model to the JSON object
    // cJSON_AddStringToObject(root, "ASICModel", GLOBAL_STATE.DEVICE_CONFIG.family.asic.name);
    // cJSON_AddStringToObject(root, "deviceModel", GLOBAL_STATE.DEVICE_CONFIG.family.name);
    // cJSON_AddStringToObject(root, "swarmColor", GLOBAL_STATE.DEVICE_CONFIG.family.swarm_color);
    // cJSON_AddNumberToObject(root, "asicCount", GLOBAL_STATE.DEVICE_CONFIG.family.asic_count);

    // cJSON_AddNumberToObject(root, "defaultFrequency", GLOBAL_STATE.DEVICE_CONFIG.family.asic.default_frequency_mhz);

    // // Create arrays for frequency and voltage options based on ASIC model
    // cJSON *freqOptions = cJSON_CreateArray();
    // size_t count = 0;
    // while (GLOBAL_STATE.DEVICE_CONFIG.family.asic.frequency_options[count] != 0) {
    //     cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(GLOBAL_STATE.DEVICE_CONFIG.family.asic.frequency_options[count]));
    //     count++;
    // }
    // cJSON_AddItemToObject(root, "frequencyOptions", freqOptions);

    // cJSON_AddNumberToObject(root, "defaultVoltage", GLOBAL_STATE.DEVICE_CONFIG.family.asic.default_voltage_mv);

    // cJSON *voltageOptions = cJSON_CreateArray();
    // count = 0;
    // while (GLOBAL_STATE.DEVICE_CONFIG.family.asic.voltage_options[count] != 0) {
    //     cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(GLOBAL_STATE.DEVICE_CONFIG.family.asic.voltage_options[count]));
    //     count++;
    // }
    // cJSON_AddItemToObject(root, "voltageOptions", voltageOptions);

    // const char *response = cJSON_Print(root);
    // httpd_resp_sendstr(req, response);

    // free((void *)response);
    // cJSON_Delete(root);
    return w->result;
}
