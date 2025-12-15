#pragma once

#include <cstdint>
#include <string_view>
#include <span>

namespace hex {

    namespace {

        inline constexpr char HEXCHARS[] = 
                {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

        inline unsigned hexchar2bin(const unsigned ch) {
            constexpr uint32_t MASK = ~(1<<5);
            constexpr uint32_t A_OFF = 'A'-10; // Mapping 'A' to 10
            constexpr uint32_t Z_OFF = '0' & MASK;
            int32_t x = (ch & ~(1<<5)); // Turns lower into upper case, and moves '0' to 0x10
            x = x - A_OFF; // Map 'A' to 10.
            uint32_t a = (x >> 31) & (A_OFF-Z_OFF);
            return x+a;
        }

        inline unsigned hex2u8(const char* const hex) {
            return (hexchar2bin(hex[0]) << 4) | hexchar2bin(hex[1]);
        }

    }

    inline uint32_t hex2u32(const std::string_view hex) {
        uint32_t u32 {0};
        unsigned numchars = (2*sizeof(u32)) < hex.size() ? (2*sizeof(u32)) : hex.size();
        const char* ptr = hex.data();
        const char* const end = ptr + numchars;
        while(ptr < end) {
            u32 = (u32<<8) | hex2u8(ptr);
            ptr += 2;
        }
        return u32;
    }

    inline std::size_t hex2bin(const char* hex, uint8_t* const out_bin, size_t maxBytes) {
        uint8_t* const end = out_bin + maxBytes;
        uint8_t* out = out_bin;
        while(out < end && hex[0] != '\0' && hex[1] != '\0') {
            *out = hex2u8(hex);
            out += 1;
            hex += 2;
        }
        if(out >= end || hex[0] == '\0') {
            return out - out_bin;
        } else {
            // Odd number of characters...
            return 0;
        }
    }

    inline std::size_t hex2bin(const std::string_view& hex, uint8_t* const out_bin, const std::size_t maxBytes) {
        const std::size_t byteLen = (maxBytes < (hex.size()/2)) ? maxBytes : (hex.size()/2);

        uint8_t* pout = out_bin;
        uint8_t* const end = pout + byteLen;
        const char* hx = hex.data();

        while(pout < end) {
            *pout = hex2u8(hx);
            pout += 1;
            hx += 2;
        }

        return byteLen;
    }

    inline std::size_t hex2bin(const std::string_view& hex, const std::span<uint8_t>& out) {
        return hex2bin(hex, out.data(), out.size_bytes());
    }

}