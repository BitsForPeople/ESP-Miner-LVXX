#ifndef INC_CRC_H_
#define INC_CRC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t crc5(const uint8_t *data, uint8_t len);
uint16_t crc16(const uint8_t *data, uint16_t len);
uint16_t crc16_false(const uint8_t *data, uint16_t len);

static inline bool crc5_valid(const uint8_t* const data, const unsigned len) {
    return crc5(data,len) == 0;
	// if(len > 1) {
	// 	return crc5(data,len-1) == data[len-1];
	// } else {
	// 	return false;
	// }
}

#ifdef __cplusplus
}
#endif

#endif /* INC_CRC_H_ */
