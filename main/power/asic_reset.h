#ifndef ASIC_RESET_H_
#define ASIC_RESET_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void set_asic_reset(bool en);

static inline esp_err_t asic_reset(void) {
    set_asic_reset(true);
    vTaskDelay(100/portTICK_PERIOD_MS);
    set_asic_reset(false);
    vTaskDelay(100/portTICK_PERIOD_MS);    
    return ESP_OK;
}

#endif /* ASIC_RESET_H_ */
