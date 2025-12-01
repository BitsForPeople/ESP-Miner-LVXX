#pragma once
#include <sdkconfig.h>
#ifdef __cplusplus

    #include <cstdint>
    #include <type_traits>

    namespace mem_cpy {
        namespace {
            template<typename T, typename P>
            static inline T& as(P* ptr) {
                return *((T*)(ptr));
            }
            
            template<typename T>
            static inline T* pplus(T* const ptr, int i) {
                return (T*)((uintptr_t)ptr + i);
            }
            
            template<typename T>
            static inline void incp(T*& ptr, int i) {
                ptr = (T*)((uintptr_t)ptr + i);
            }
            
            static inline bool known(bool cond) {
                return __builtin_constant_p(cond) && cond;
            }
            
            static inline bool maybe(bool cond) {
                return !known(!cond);
            }
            
            template<uint32_t D>
            static constexpr uint32_t div(const uint32_t x) {
                if(std::is_constant_evaluated() || __builtin_constant_p(x/D)) {
                    return x/D;
                } else {
                    uint32_t d = D;
                    asm ("":"+r"(d));
                    const uint32_t r = x/d;
                    [[assume(r == (x/D))]];
                    return r;
                }
            }
            
            template<uint32_t D>
            static constexpr uint32_t rem(const uint32_t x) {
                if(std::is_constant_evaluated() || __builtin_constant_p(x%D)) {
                    return x%D;
                } else {
                    uint32_t d = D;
                    asm ("":"+r"(d));
                    const uint32_t r = x%d;
                    [[assume(r == (x % D))]];
                    [[assume(r < D)]];
                    return r;
                }
            }

            static constexpr uint32_t min(const uint32_t a, const uint32_t b) {
                return (a<b) ? a : b;
            }
        } // anon namespace

        template<typename T, typename S, typename D>
        static inline void cpyAs(const S*& src, D*& dst) {
            as<T>(dst) = as<const T>(src);
            incp(dst,sizeof(T));
            incp(src,sizeof(T));
        }
        
        template<typename S, typename D>
        static inline void cpyshrt(const S*& src, D*& dst, const uint32_t cnt) {
            if(cnt & 8) {
                cpyAs<uint64_t>(src,dst);
            }
            if(cnt & 4) {
                cpyAs<uint32_t>(src,dst);
            }
            if(cnt & 2) {
                cpyAs<uint16_t>(src,dst);  
            }
            if(cnt & 1) {
                cpyAs<uint8_t>(src,dst);
            }    
        }
        
        inline void simdCpy(const void* src, void* dst, uint32_t cnt) {
        
            if(maybe(cnt >= 16)) {
                {
                    uint32_t off = ((uintptr_t)dst + 15) & ~0xf;
                    if(off != (uintptr_t)dst) {
                        off = off - (uintptr_t)dst;
                        [[assume(off < 16)]];
                        uint32_t c = min(off,cnt);
                        cnt -= c;
                        cpyshrt(src,dst,c);
                    }
                }
                // dst = __builtin_assume_aligned(dst,16);
        
                if (cnt >= 16) {
                    asm (
                        "EE.LD.128.USAR.IP q0, %[src], 16" "\n"
                        "EE.VLD.128.IP q1, %[src], 16" "\n"
                        : [src] "+r" (src)
                        : "m" (*(const uint8_t(*)[cnt])src)
                    );
        
                    if(cnt >= 48) {
                        // Copy 48-byte chunks @ 2 clocks/vector
                        const uint32_t i = div<48>(cnt);
        
                        asm (
                            "LOOPNEZ %[i], .Lend_%=" "\n"
                                "EE.SRC.Q.LD.IP q2, %[src], 16, q0, q1" "\n"
                                "EE.VST.128.IP q0, %[dst], 16" "\n"
                                "EE.SRC.Q.LD.IP q0, %[src], 16, q1, q2" "\n"
                                "EE.VST.128.IP q1, %[dst], 16" "\n"
                                "EE.SRC.Q.LD.IP q1, %[src], 16, q2, q0" "\n"
                                "EE.VST.128.IP q2, %[dst], 16" "\n"
                            ".Lend_%=:"
                            : [src] "+r" (src), [dst] "+r" (dst),
                            "=m" (*(uint8_t(*)[cnt])dst)
                            : [i] "r" (i),
                            "m" (*(const uint8_t(*)[cnt])src)
                        );
                        cnt = rem<48>(cnt);
                    }
                    if(maybe((cnt/16) != 0)) {
                        // Copy remaining 0, 1, or 2 vectors @ 3 clocks/vector:
                        asm (
                            "LOOPNEZ %[i], .Lend_%=" "\n"
                                "EE.SRC.Q.QUP q2, q0, q1" "\n"
                                "EE.VLD.128.IP q1, %[src], 16" "\n"
                                "EE.VST.128.IP q2, %[dst], 16" "\n"
                            ".Lend_%=:"
                            : [src] "+r" (src), [dst] "+r" (dst),
                            "=m" (*(uint8_t(*)[cnt])dst)
                            : [i] "r" (cnt/16),
                            "m" (*(const uint8_t(*)[cnt])src)
                        );
                    }
        
                    incp(src,-32);
        
                    // dst = __builtin_assume_aligned(dst,16);
                    if(cnt & 8) {
                        asm (
                            "EE.SRC.Q.QUP q2, q0, q1" "\n"
                            "EE.VST.L.64.IP q2, %[dst], 8" "\n"
                            : [dst] "+r" (dst),
                            "=m" (*(uint8_t(*)[cnt])dst)
                            :
                        );
                        // dst = __builtin_assume_aligned(dst,8);
                        incp(src,8);
                        cnt -= 8;
                    }
        
                }
            }
        
            if(cnt & 0xf) {
                cpyshrt(src,dst,(cnt&0xf));
            }
        }
    }

    #if CONFIG_IDF_TARGET_ESP32S3
        extern "C" void cpy_mem(const void* src, void* dst, uint32_t cnt);
    #else
        #include <cstring>
        static inline void cpy_mem(const void* src, void* dst, uint32_t cnt) {
            memcpy(dst,src,cnt);
        }
    #endif // S3

#else // if C++

    #include <stdint.h>
    #include <string.h>

    #if CONFIG_IDF_TARGET_ESP32S3
        void cpy_mem(const void* src, void* dst, uint32_t cnt);
    #else
        static inline void cpy_mem(const void* src, void* dst, uint32_t cnt) {
            memcpy(dst,src,cnt);
        }
    #endif // S3

#endif // if C++

