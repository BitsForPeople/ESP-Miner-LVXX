#pragma once
#include <cstdint>

namespace crc {

    class Crc5 {
        /*
         * We need to align bit #7 of data with bit #4 of the crc to get the next CRC bit:
         * bit = (((crc >> 4) ^ (data >> 7)) & 1) != 0;
         * By left-shifting the CRC value by (7-4) bits we pre-align the crc with the data bits
         * and save two right shifts per bit of data processed.
         * bit = ((crc ^ data) & (1<<(4+(7-4))) != 0; (Bit #4 of the crc stays at position 4+(7-4))
         * 
         * (Note: We work with uint32_t variables (instead of uint8_t or uint16_t) for performance.)
         */
        static constexpr unsigned ALIGN_SHIFT = (7-4);
        // Poly x⁵ + x² + 1 MSB-first
        static constexpr unsigned POLY_BITS = ((1<<2)+(1<<0)) << ALIGN_SHIFT;

        uint32_t state {crc5_init()};

        public:

        static constexpr uint8_t crc5(const uint8_t *data, const uint32_t len) {
            const uint32_t crc = crc5_upd(crc5_init(),data,len);
            return crc5_finish(crc);
        }

        constexpr Crc5() = default;
        constexpr Crc5(const uint32_t initVal) : state {crc5_init(initVal)} {

        }

        constexpr Crc5& reset(const uint32_t initVal = 0x1f) {
            this->state = crc5_init(initVal);
            return *this;
        }

        constexpr Crc5& upd(const uint32_t byte) {
            this->state = crc5_upd(this->state,byte);
            return *this;
        }

        constexpr Crc5& upd(const uint8_t* data, const uint32_t len) {
            this->state = crc5_upd(this->state,data,len);
            return *this;
        }

        constexpr uint8_t value() const noexcept {
            return crc5_finish(this->state);
        }

        private:

            static constexpr uint32_t crc5_init(const uint32_t crc = 0x1f) {
                return crc << ALIGN_SHIFT;
            }

            static constexpr uint8_t crc5_finish(const uint32_t crc) {
                return (crc >> ALIGN_SHIFT) & 0x1f;
            }

            static constexpr uint32_t crc5_upd(uint32_t crc, uint32_t data) {
                for(unsigned i = 8; i != 0; --i) {
                    const uint32_t c = crc ^ data;
                    crc = (crc << 1);
                    if(c & (1u << (ALIGN_SHIFT+4))) {
                        crc ^= POLY_BITS;
                    }
                    data = data << 1;
                }
                return crc;
            }            


            static constexpr uint32_t crc5_upd(uint32_t crc, const uint8_t *data, const uint32_t len) {
                const uint8_t* const end = data + len;

                while (data < end) {
                    crc = crc5_upd(crc,*data);
                    ++data;
                }
                // return (crc>>ALIGN_SHIFT) & 0x1f;
                return crc;
            }
    
    };
}