#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "common.h"
#include "serial.h"
#include "esp_log.h"
#include "crc.h"

#define PREAMBLE 0xAA55

static const char * const TAG = "common";

static const uint8_t READ_REG0_CMD[] = {0x55, 0xAA, 0x52, 0x05, 0x00, 0x00, 0x0A};

static const uint16_t RX_PREAMBL = 0x55AA;

static const uint16_t RESP_WAIT_MS = 100;


typedef struct __attribute__((packed)) ReadRegBase {
    uint16_t preamble;
    uint16_t regVal1;
    uint16_t regVal2;
    uint8_t chipAddr;
    uint8_t regAddr;
} ReadRegBase_t;

typedef struct __attribute__((packed)) ReadRegRspShrt {
    ReadRegBase_t base;
    uint8_t crc;
} ReadRegRspShrt_t;

static_assert(sizeof(ReadRegRspShrt_t) == 9);

typedef struct __attribute__((packed)) ReadRegRspLong {
    ReadRegBase_t base;
    uint16_t extraBytes;
    uint8_t crc; // If this comes back as 0x55 it's not a CRC5 (<=0x1f) but already the 2nd byte of a preamble of the next response.
} ReadRegRspLong_t;

static_assert(sizeof(ReadRegRspLong_t) == 11);

static inline bool isLongResponse(const uint8_t* const data, const unsigned len) {
    return (len >= sizeof(ReadRegRspLong_t)) && (((const ReadRegRspLong_t*)data)->crc <= 0x1f);
}

static inline bool isValidResponse(const uint8_t* const data, const unsigned len) {
    return (len >= sizeof(ReadRegRspShrt_t)) &&
           ((const ReadRegBase_t*)data)->preamble == 0x55AA &&
           crc5_valid(data+2,len-2);
}

static inline bool isValidLongResponse(const uint8_t* const data, const unsigned len) {
    return isLongResponse(data,len) && 
           isValidResponse(data,sizeof(ReadRegRspLong_t)); 
}

static inline bool isValidShrtResponse(const uint8_t* const data, const unsigned len) {
    return (len >= sizeof(ReadRegRspShrt_t)) &&
           isValidResponse(data,sizeof(ReadRegRspShrt_t));     
}

static inline int rx(uint8_t* const out, unsigned len) {
    int r = SERIAL_rx(out,len,RESP_WAIT_MS);
    if(r < 0) {
        ESP_LOGE(TAG, "ASIC_detect: Error reading ASIC response: %d",r);
    }
    return r;
}



static int countChipResponses(uint32_t rspLen, uint8_t* const buf) {
    int cnt = 0;

    int r;
    do {
        const uint16_t pre = *(const uint16_t*)buf;
        if( pre == RX_PREAMBL) {
            if(crc5_valid(buf+2, rspLen-2)) {
                cnt += 1;
            } else {
                ESP_LOGE(TAG, "ASIC_detect: CRC error in response!");
                return -cnt;
            }
        } else {
            ESP_LOGE(TAG, "ASIC_detect: Invalid preamble: 0x%" PRIx16, pre);
            return -cnt;
        }
    } while ( (r = rx( buf, rspLen )) == rspLen );

    if(r == 0) {
        return cnt;
    } else {
        if(r > 0) {
            ESP_LOGE(TAG, "ASIC_detect: Error reading ASIC response: %" PRIu32 " bytes expected, %d received.",
                rspLen,
                r
            );
        }
        return -cnt;
    }
}

int ASIC_detect(uint16_t* const out_chip_id) {



    static const unsigned RSP_SHRT = sizeof(ReadRegRspShrt_t);
    static const unsigned RSP_LONG = sizeof(ReadRegRspLong_t);
    static const unsigned RSP_DIFF = RSP_LONG - RSP_SHRT;


    SERIAL_send(READ_REG0_CMD,sizeof(READ_REG0_CMD),false);

    // union {
    //     ReadRegBase_t base;
    //     ReadRegRspLong_t lng; 
    //     ReadRegRspShrt_t shrt[2];
    //     uint8_t u8[sizeof(ReadRegRspShrt_t)*2];
    // } rxBuf_;

    // uint8_t* buf = rxBuf_.u8;
    // int r = SERIAL_rx(buf,RSP_LONG,100);

    // if(r >= RSP_SHRT) {
    //     unsigned rspLen = RSP_SHRT;

    //     if(isValidLongResponse(rxBuf,r)) {
    //         rspLen = RSP_LONG;
    //     } else
    //     if(isValidShrtResponse(rxBuf,r)) {
    //         if(r > RSP_SHRT) {
    //             buf = (uint8_t*)&(rxBuf_.shrt[1]);
    //         }
    //     } else {
    //         ESP_LOGE(TAG, "Fail.");
    //     }
    // }

    union {
        ReadRegBase_t base;
        ReadRegRspShrt_t shrt;
        ReadRegRspLong_t lng;
        uint8_t u8[RSP_LONG];
    } rxBuf;

    int r = rx(rxBuf.u8,RSP_LONG);

    int cnt = 0;

    if(r >= 0) {
        if(r >= RSP_SHRT) {
            if(rxBuf.base.preamble == RX_PREAMBL) {
                const bool isLong = (r == RSP_LONG) && (rxBuf.lng.crc <= 0x1f);
                const unsigned rspLen = isLong ? RSP_LONG : RSP_SHRT;
            
                if(crc5_valid(rxBuf.u8+2, rspLen-2)) {
                    cnt = 1;

                    if(out_chip_id) {
                        *out_chip_id = __builtin_bswap16(rxBuf.base.regVal1);
                    }
                    ESP_LOGD(TAG, "ASIC_detect: Found chip 0x%" PRIx16, __builtin_bswap16(rxBuf.base.regVal1));

                    // All good handling the first response. Prepare for any next response(s).
                    // if (r == RSP_LONG) {
                    //     if(!isLong) {
                    //         // Read the rest of the 2nd chip's short response:
                    //         r = SERIAL_rx( rxBuf.u8 + RSP_DIFF, rspLen-RSP_DIFF, 100 );
                    //         if(r >= 0) {
                    //             r += RSP_DIFF;
                    //         }
                    //     } else {
                    //         // Read the 2nd chip's long response:
                    //         r = SERIAL_rx( rxBuf.u8, rspLen, 100);
                    //     }

                    //     if(r == rspLen) {
                    //         // Got one more. Check and count this and any other responses.
                    //         int c = countChipResponses(rspLen,rxBuf.u8);
                    //         if(c >= 0) {
                    //             cnt = cnt + c;
                    //         } else {
                    //             cnt = -cnt + c;
                    //         }
                    //     } else {
                    //         if(r < 0) {
                    //             ESP_LOGE(TAG, "ASIC_detect: Error reading response: %d",r);
                    //         }
                    //         if(!isLong) {
                    //             // We got more data than 1 short response, but not enough for 2?!
                    //             ESP_LOGW(TAG, "ASIC_detect: Partial response: %d");
                    //         }
                    //     }
                    // }
                    if (r == RSP_LONG) {
                        if(!isLong) {
                            // We read too many bytes. Correct for that.
                            if(RSP_DIFF > 2) { // The preamble of 2 bytes is already there.
                                memcpy(rxBuf.u8+2, rxBuf.u8+RSP_SHRT+2, (RSP_DIFF-2));
                            }
                            // Now, the first RSP_DIFF bytes of the next response are already properly located in the buffer.
                            r = rx( rxBuf.u8 + RSP_DIFF, RSP_SHRT-RSP_DIFF );
                            if(r >= 0) {
                                r += RSP_DIFF;
                            }
                        } else {
                            // Just read the next response.
                            r = rx( rxBuf.u8, rspLen );
                        }

                        bool err = false;
                        while(r == rspLen) {
                            if(rxBuf.base.preamble != RX_PREAMBL) {
                                err = true;
                                break;
                            } else
                            if (!crc5_valid(rxBuf.u8+2,rspLen-2)) {
                                err = true;
                                break;
                            }
                            ++cnt;
                            r = rx( rxBuf.u8, rspLen );
                        }
                        if(err) {
                            ESP_LOGE(TAG, "ASIC_detect: Error at chip #%d", cnt);
                            cnt = -cnt;
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "ASIC_detect: CRC error in response!");
                }    
            } else {
                ESP_LOGE(TAG, "ASIC_detect: Invalid preamble: 0x%" PRIx16, rxBuf.base.preamble);
            }            
        } else {
            ESP_LOGE(TAG, "ASIC_detect: Failed to get ASIC response (got %d bytes)",r);
        }
        if(cnt <= 0) {
            // Something went wrong.
            // Consume any remaining response(s) from the serial:
            while(SERIAL_rx(rxBuf.u8,sizeof(rxBuf.u8),10) > 0) {

            }
        }
    } else {
        ESP_LOGE(TAG, "ASIC_detect: Error reading ASIC response: %d",r);
    }
    return cnt;
}

int count_asic_chips(uint16_t asic_count, uint16_t chip_id, int chip_id_response_length)
{
    uint8_t buffer[11] = {0};

    int chip_counter = 0;
    while (true) {
        int received = SERIAL_rx(buffer, chip_id_response_length, 1000);
        if (received == 0) break;

        if (received == -1) {
            ESP_LOGE(TAG, "Error reading CHIP_ID");
            break;
        }

        if (received != chip_id_response_length) {
            ESP_LOGE(TAG, "Invalid CHIP_ID response length: expected %d, got %d", chip_id_response_length, received);
            ESP_LOG_BUFFER_HEX(TAG, buffer, received);
            break;
        }

        uint16_t received_preamble = (buffer[0] << 8) | buffer[1];
        if (received_preamble != PREAMBLE) {
            ESP_LOGW(TAG, "Preamble mismatch: expected 0x%04x, got 0x%04x", PREAMBLE, received_preamble);
            ESP_LOG_BUFFER_HEX(TAG, buffer, received);
            continue;
        }

        uint16_t received_chip_id = (buffer[2] << 8) | buffer[3];
        if (received_chip_id != chip_id) {
            ESP_LOGW(TAG, "CHIP_ID response mismatch: expected 0x%04x, got 0x%04x", chip_id, received_chip_id);
            ESP_LOG_BUFFER_HEX(TAG, buffer, received);
            continue;
        }

        if (crc5(buffer + 2, received - 2) != 0) {
            ESP_LOGW(TAG, "Checksum failed on CHIP_ID response");
            ESP_LOG_BUFFER_HEX(TAG, buffer, received);
            continue;
        }

        ESP_LOGI(TAG, "Chip %d detected: CORE_NUM: 0x%02x ADDR: 0x%02x", chip_counter, buffer[4], buffer[5]);

        chip_counter++;
    }    
    
    if (chip_counter != asic_count) {
        ESP_LOGW(TAG, "%i chip(s) detected on the chain, expected %i", chip_counter, asic_count);
    }

    return chip_counter;
}

