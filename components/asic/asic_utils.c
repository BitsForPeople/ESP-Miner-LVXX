#include "esp_log.h"
#include "asic_utils.h"
#include "serial.h"
#include "crc.h"

static const char* const TAG = "asic_utils";


esp_err_t ASIC_receive_work(uint8_t * buffer, int buffer_size)
{
    static const uint16_t RX_PREAMBL = 0x55AA;

    int received = SERIAL_rx(buffer, buffer_size, 10000);

    if (received < 0) {
        ESP_LOGE(TAG, "UART error in serial RX");
        return ESP_FAIL;
    }

    if (received == 0) {
        ESP_LOGD(TAG, "UART timeout in serial RX");
        return ESP_FAIL;
    }

    if (received != buffer_size) {
        ESP_LOGE(TAG, "Invalid response length %i", received);
        ESP_LOG_BUFFER_HEX(TAG, buffer, received);
        SERIAL_clear_buffer();
        return ESP_FAIL;
    }

    const uint16_t received_preamble = *(const uint16_t*)buffer;
    if (received_preamble != RX_PREAMBL) {
        ESP_LOGE(TAG, "Preamble mismatch: got 0x%" PRIx16 ", expected 0x%" PRIx16, received_preamble, RX_PREAMBL);
        ESP_LOG_BUFFER_HEX(TAG, buffer, received);
        SERIAL_clear_buffer();
        return ESP_FAIL;
    }

    if (!crc5_valid(buffer + 2, buffer_size - 2)) {
        ESP_LOGE(TAG, "Checksum failed on response");        
        ESP_LOG_BUFFER_HEX(TAG, buffer, received);
        SERIAL_clear_buffer();
        return ESP_FAIL;
    }

    return ESP_OK;
}


void asic_cpy_hash_reverse_words(const void* const src, void* dst) {
    uint32_t* d = (uint32_t*)dst;
    uint32_t* const end = d + 8;
    const uint32_t* s = ((const uint32_t*)src) + 8 - 1;
    do {
        *d = *s;
        ++d;
        --s;
    } while(d < end);
}

