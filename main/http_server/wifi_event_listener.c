#include <stdint.h>
#include "wifi_event_listener.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

static const char* const TAG = "wifi_event_listener";

static esp_event_handler_instance_t inst;

// static const esp_event_base_t EVT_BASE = WIFI_EVENT;
// static const int32_t EVT_ID = ESP_EVENT_ANY_ID;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void * event_data) {
    WifiEventListenerCtx_t* const ctx = (WifiEventListenerCtx_t*)arg;
    if (event_base == WIFI_EVENT) {
        if(event_id == WIFI_EVENT_AP_STOP) {
            ESP_LOGI(TAG, "AP stopped.");
            if(ctx->dnsHdl) {
                ESP_LOGI(TAG, "Stopping DNS server.");
                if (stop_dns_server(ctx->dnsHdl)) {
                    ctx->dnsHdl = NULL;
                }
            }
        }
        //  else
        // if(event_id == WIFI_EVENT_AP_START) {
        //     // if(!ctx->dnsHdl) {
        //     //     ctx->dnsHdl = start_dns_server()
        //     // }
        // }
    }
}

bool wifi_event_listener_start(WifiEventListenerCtx_t* const ctx) {
    esp_err_t r = esp_event_handler_instance_register(WIFI_EVENT,WIFI_EVENT_AP_STOP,&wifi_event_handler,ctx,&inst);
    if(r == ESP_OK) {
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %d",r);
        return false;
    }
}

void wifi_event_listener_stop(void) {
    if(inst) {
        esp_err_t r = esp_event_handler_instance_unregister(WIFI_EVENT,WIFI_EVENT_AP_STOP,inst);
        if(r == ESP_OK) {
            inst = NULL;
        }
    }
}

