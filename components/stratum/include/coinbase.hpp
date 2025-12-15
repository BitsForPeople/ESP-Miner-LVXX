#pragma once

#include <cstdint>
#include <span>
#include <cstring>
#include "mem_cpy.h"
#include "coinbase.h"

// static_assert(0 < (COINBASE_MAX_SIZE) && (COINBASE_MAX_SIZE) < 65536);

// class CB : public CB_mem {
//     public:
//         static constexpr uint16_t MAX_SIZE = (COINBASE_MAX_SIZE);

//         constexpr CB() = default;
//         constexpr CB(const CB& other) {
//             this->operator =(other);
//         }
//         constexpr CB& operator =(const CB& other) {
//             if(this != &other) {
//                 this->sz = other.sz;
//                 this->xn2Pos = other.xn2Pos;
//                 this->xn2Len = other.xn2Len;
//                 if(this->sz != 0) {
//                     std::memcpy(this->u8, other.u8, this->sz);
//                 }
//             }
//             return *this;
//         };

//         constexpr std::size_t copyTo(uint8_t* out) const {
//             if(this->sz != 0) {
//                 std::memcpy(out,this->u8,this->sz);
//             }
//             return this->sz;
//         }

//         constexpr CB& reset() {
//             this->sz = 0;
//             this->xn2Pos = 0;
//             this->xn2Len = 0;
//             return *this;
//         }

//         constexpr CB& placeXn2(const std::size_t pos, const std::size_t len) {
//             if((pos+len) < this->sz) {
//                 this->xn2Pos = pos;
//                 this->xn2Len = len;
//             }
//             return *this;
//         }

//         constexpr uint64_t getXn2() const {
//             if(xn2Len > 0) {
//                 if (std::is_constant_evaluated()) {
//                     uint64_t xn {0};
//                     for(unsigned i = 1; i <= xn2Len; ++i) {
//                         xn = xn << 8;
//                         xn = xn | u8[xn2Pos+xn2Len-i];
//                     }
//                     return xn;
//                 } else {
//                     uint64_t xn = *(const uint64_t*)(xn2ptr());
//                     xn = xn & getXn2Mask();
//                     return xn;
//                 }
//             } else {
//                 return 0;
//             }
//         }

//         void xn2To(Nonce_t& out_nonce) const {
//             size_t len = sizeof(out_nonce.u8) < this->xn2Len ? sizeof(out_nonce.u8) : this->xn2Len;
//             std::memcpy(out_nonce.u8,this->xn2ptr(),len);
//             out_nonce.size = len;
//         }

//         constexpr CB& setXn2(uint64_t xn2) {
//             unsigned len = xn2Len;
//             if(len > 0) {
//                 len -= st32(xn2ptr(), (uint32_t)xn2, len);
//                 if(len != 0) {
//                     st32(xn2ptr() + sizeof(uint32_t), (xn2 >> 32), len );
//                 }
//             }
//             return *this;
//         }



//         constexpr std::span<uint8_t> span() {
//             return std::span<uint8_t> {u8,sz};
//         }

//         constexpr std::span<const uint8_t> span() const {
//             return std::span<const uint8_t> {u8,sz};
//         }

//         constexpr operator std::span<uint8_t>() {
//             return span();
//         }

//         constexpr operator std::span<const uint8_t>() const {
//             return span();
//         }

//         constexpr uint32_t size() const {
//             return sz;
//         }

//         constexpr uint8_t* data() {
//             return u8;
//         }

//         constexpr const uint8_t* data() const {
//             return u8;
//         }

//         constexpr uint32_t setSize(const uint32_t sz) {
//             if(sz <= MAX_SIZE) {
//                 this->sz = sz;
//                 return sz;
//             } else {
//                 return MAX_SIZE;
//             }
//         }

//         constexpr bool addSize(uint32_t sz) {
//             if(sz <= space()) {
//                 this->sz = this->sz + sz;
//                 return true;
//             } else {
//                 return false;
//             }
//         }

//         constexpr uint32_t append(const std::span<const uint8_t>& data) {
//             const std::size_t len = min(data.size_bytes(), MAX_SIZE - this->sz);
//             if(std::is_constant_evaluated()) {
//                 for(std::size_t i = 0; i < len; ++i) {
//                     u8[this->sz+i] = data[i];
//                 }
//             } else {
//                 std::memcpy(this->u8+this->sz, data.data(), len);
//             }
//             this->sz += len;
//             return len;
//         }

//         constexpr uint32_t append(const unsigned byte) {
//             if(this->sz < MAX_SIZE) {
//                 this->u8[this->sz] = byte;
//                 this->sz += 1;
//                 return 1;
//             } else {
//                 return 0;
//             }
//         }

//         constexpr uint32_t appendXn2Space(const std::size_t sz) {
//             if((sz <= sizeof(uint64_t)) && ((MAX_SIZE-sz) >= this->sz)) {
//                 this->xn2Pos = this->sz;
//                 this->xn2Len = sz;
//                 this->sz += sz;
//                 setXn2(0);
//                 return sz;
//             } else {
//                 return 0;
//             }
//         }

//         uint8_t* tail(void) {
//             return this->u8 + this->sz;
//         }

//         std::size_t space(void) const {
//             return MAX_SIZE - this->sz;
//         }

//     private:
//         // uint16_t sz {0};

//         // uint16_t xn2Pos {0};
//         // uint8_t xn2Len {0};

//         // uint8_t u8[N] {};

//         static constexpr unsigned st32(uint8_t* p, uint32_t v, unsigned cnt) {
//             if(cnt >= sizeof(uint32_t) && !std::is_constant_evaluated()) {
//                 *(uint32_t*)p = v;
//                 return sizeof(uint32_t);
//             } else {
//                 for(unsigned i = 0; i < cnt; ++i) {
//                     *p = v;
//                     ++p;
//                     v = v >> 8;
//                 }
//                 return cnt;
//             }
//         }

//         uint8_t* xn2ptr() {
//             return this->u8 + this->xn2Pos;
//         }

//         const uint8_t* xn2ptr() const {
//             return this->u8 + this->xn2Pos;
//         }

//         constexpr uint64_t getXn2Mask(void) const {
//             return getMask(xn2Len);
//         }

//         static constexpr uint64_t getMask(const uint32_t n) {
//             uint32_t lo = -1;
//             uint32_t hi = -1;
//             if(n < sizeof(uint64_t)) {
//                 if(n <= sizeof(uint32_t)) {
//                     hi = 0;
//                     if(n < sizeof(uint32_t)) {
//                         lo = ~(((uint32_t)-1) << (n*8));
//                     }
//                 } else {
//                     hi = ~(((uint32_t)-1) << ((n-sizeof(uint32_t))*8));
//                 }
//             }
//             return (((uint64_t)hi) << 32) | lo;
//             // uint64_t m = -1;
//             // if(n < sizeof(uint64_t)) {
//             //     m = ~(((uint32_t)-1) << (n*8)); // m >> ((sizeof(uint32_t)-n)*8);
//             // }
//             // // ~(((uint32_t)-1) << (n*8));
//             // return m;
//         }

//         static constexpr uint32_t min(const uint32_t a, const uint32_t b) {
//             return (a<b) ? a : b;
//         }
// };

// using CB_t = CB;

// static_assert(sizeof(CB_t) == sizeof(struct CB_mem));