#include "mem_search.h"

// #include <type_traits>
#include <limits>

static constexpr bool known(const bool cond) noexcept {
    return __builtin_constant_p(cond) && cond;
}

static constexpr bool maybe(const bool cond) noexcept {
    return !known(!cond);

}

/* MAC-ing a comparison result with this vector yields a value < 0
   iff the first 'true' value is in the 2nd half.
   Any 'true' in the first half contributes 32 to the result, while
   all 'true' values in the second half together contribute at most -8.
*/
alignas(16)
static const int8_t POS_VEC[16] =
    { -32, -32, -32, -32, -32, -32, -32, -32,
        1,   1,   1,   1,   1,   1,   1,   1}; // 1st half values must be < than (2*(-8))x 2nd half values

template<typename T>
static inline void incptr(T*& p, int i) {
    p = (T*)((uintptr_t)p + i);
}

static inline constexpr uint32_t min(const uint32_t a, const uint32_t b) {
    return (a<b) ? a : b;
}

static inline uintptr_t headroom(const void* mem) {
    uintptr_t m = (uintptr_t)mem;
    return std::numeric_limits<uintptr_t>::max() - m;
}
template<typename T>
static inline T* find_u8_t(const T* mem, unsigned maxLen, const uint8_t value) {
    if(maxLen > 0) [[likely]] {
        maxLen = min(maxLen, headroom(mem));
        uintptr_t end = (uintptr_t)mem + maxLen;

        if(maxLen > 8) {
            uint32_t dummy;
            if(known(value == 0)) {
                asm (
                    "EE.ZERO.Q q6"
                    : "=m" (dummy)
                );
            } else {
                asm (
                    "EE.VLDBC.8 q6, %[v]"
                    : "=m" (dummy)
                    : [v] "r" (&value), "m" (value)
                );
            }
            

            asm (
                "LD.QR q7, %[pos], 0" "\n"
                : "+m" (dummy)
                : [pos] "r" (&POS_VEC), "m" (POS_VEC)
            );
            asm (
                "EE.LD.128.USAR.IP q0, %[mem], 16" "\n"
                "EE.VLD.128.IP q1, %[mem], 16" "\n"
                "EE.ZERO.ACCX" "\n"
                : [mem] "+r" (mem), "+m" (dummy)
                : "m" (*(const uint8_t(*)[maxLen])mem)
            );

            uint32_t tmp = ((maxLen+15)/16);

            asm (
                "LOOPNEZ %[tmp], .Lend_%=" "\n"
                    "EE.SRC.Q.QUP q2, q0, q1" "\n"

                    "EE.VCMP.EQ.S8 q5, q2, q6" "\n" // == value?
                    "EE.VMULAS.S8.ACCX.LD.IP q1, %[mem], 16, q5, q7" "\n"
                    // ...
                    "RUR.ACCX_0 %[tmp]" "\n"
                    "BNEZ %[tmp], .Lfound_%=" "\n"
                ".Lend_%=:"
                    "ADDI %[mem], %[end], (3*16)" "\n"
                ".Lfound_%=:"
                    "ADDI %[mem], %[mem], -(3*16)" "\n"
                : [mem] "+r" (mem),
                  [tmp] "+r" (tmp)
                : [end] "r" (end)
            );

            if((int32_t)tmp < 0) {
                // No match in the first half of the vector
                incptr(mem,8);
            }
        }

        if((uintptr_t)mem < end) {
            uint32_t tmp;
            if(known(value == 0)) {
                asm (
                    "LOOPNEZ %[cnt], .Lend_%=" "\n"
                        "L8UI %[tmp], %[mem], 0" "\n"
                        "ADDI %[mem], %[mem], 1" "\n"
                        "BEQZ %[tmp], .Lfound_%=" "\n"
                    ".Lend_%=:" "\n"
                    ".Lfound_%=:" "\n"
                    "ADDI %[mem], %[mem], -1" "\n"
                    : [mem] "+r" (mem), [tmp] "=r" (tmp)
                    : [cnt] "r" (end-(uintptr_t)mem)
                );
            } else {
                asm (
                    "LOOPNEZ %[cnt], .Lend_%=" "\n"
                        "L8UI %[tmp], %[mem], 0" "\n"
                        "ADDI %[mem], %[mem], 1" "\n"
                        "BEQ %[tmp], %[val], .Lfound_%=" "\n"
                    ".Lend_%=:" "\n"
                    ".Lfound_%=:" "\n"
                    "ADDI %[mem], %[mem], -1" "\n"
                    : [mem] "+r" (mem), [tmp] "=r" (tmp)
                    : [cnt] "r" (end-(uintptr_t)mem),
                      [val] "r" (value)
                );
            }
        }
    }
    return const_cast<T*>(mem);    
}

void* mem_find_u8(const void* mem, const unsigned maxLen, const uint8_t value) {
    return find_u8_t(mem,maxLen,value);
}

char* mem_findStrEnd(const char* str, const unsigned maxLen) {
    return static_cast<char*>(mem_find_u8(str,maxLen,0));
}

char* mem_findInStr(const char* str, const char ch, unsigned maxLen) {
    if(maxLen > 0) {
        maxLen = min(maxLen, headroom(str));
        uintptr_t end = (uintptr_t)str + maxLen;

        if(maxLen > 8) {
            uint32_t dummy;

            // q5 = POS_VEC
            // q7[n] = ch
            // q6[n] = 0x00
            asm (
                "LD.QR q5, %[pos], 0" "\n"
                : "=m" (dummy)
                : [pos] "r" (&POS_VEC), "m" (POS_VEC)
            );

            asm (
                "EE.VLDBC.8 q7, %[ch]" "\n"
                "EE.ZERO.Q q6"
                : "+m" (dummy)
                : [ch] "r" (&ch), "m" (ch), "m" (*(const char(*)[maxLen])str)
            );
            
            // const char* end = str + maxLen;
            // if(end < str) {
            //     end = (const char*)-1;
            //     maxLen = end-str;
            // }

            uint32_t tmp = ((maxLen+15)/16);
            asm (
                "EE.LD.128.USAR.IP q0, %[str], 16" "\n"
                "EE.VLD.128.IP q1, %[str], 16" "\n"
                "EE.ZERO.ACCX" "\n"

                "EE.SRC.Q.QUP q2, q0, q1" "\n"

                "LOOPNEZ %[tmp], .Lend_%=" "\n"

                    "EE.VCMP.EQ.S8 q4, q2, q6" "\n" // == 0?
                    "EE.VMULAS.S8.ACCX.LD.IP q1, %[str], 16, q4, q5" "\n"

                    "EE.VCMP.EQ.S8 q5, q2, q7" "\n" // == ch?
                    "EE.VMULAS.S8.ACCX q4, q5" "\n"

                    "EE.SRC.Q.QUP q2, q0, q1" "\n"

                    "RUR.ACCX_0 %[tmp]" "\n"
                    "BNEZ %[tmp], .Lfound_%=" "\n"

                ".Lend_%=:"
                    "ADDI %[str], %[end], (3*16)" "\n" // Make str point to end
                ".Lfound_%=:"
                    "ADDI %[str], %[str], -(3*16)" "\n"
                : [str] "+r" (str),
                  [tmp] "+r" (tmp),
                  "+m" (dummy)
                : [end] "r" (end)
            );

            if((int32_t)tmp < 0) {
                incptr(str,8);
            }
        }

        if((uintptr_t)str < end) {
            uint32_t tmp;
            asm (
                "LOOPNEZ %[cnt], .Lend_%=" "\n"
                    "L8UI %[tmp], %[str], 0" "\n"
                    "ADDI %[str], %[str], 1" "\n"
                    "BEQ %[tmp], %[ch], .Lfound_%=" "\n"
                    "BEQZ %[tmp], .Lfound_%=" "\n"
                ".Lend_%=:" "\n"
                ".Lfound_%=:" "\n"
                : [str] "+r" (str), [tmp] "=r" (tmp)
                : [ch] "r" (ch), [cnt] "r" (end-(uintptr_t)str)
            );
            str -= 1;
        }
    }
    return const_cast<char*>(str);
}

char* mem_findLineEnd(const char* str, unsigned maxLen) {
    return mem_findInStr(str, '\n', maxLen);
    // static const char NL = '\n';
    // if(maxLen > 0) {
    //     uint32_t dummy;
    //     asm (
    //         "EE.VLDBC.8 q7, %[nl]" "\n"
    //         "EE.ZERO.Q q6"
    //         : "=m" (dummy)
    //         : [nl] "r" (&NL), "m" (NL), "m" (*(const char(*)[maxLen])str)
    //     );
        
    //     const char* end = str + maxLen;
    //     if(end < str) {
    //         end = (const char*)-1;
    //         maxLen = end-str;
    //     }

    //     uint32_t tmp = ((maxLen+15)/16);
    //     asm (
    //         "EE.LD.128.USAR.IP q0, %[str], 16" "\n"
    //         "EE.VLD.128.IP q1, %[str], 16" "\n"
    //         "EE.ZERO.ACCX" "\n"

    //         "EE.SRC.Q.QUP q2, q0, q1" "\n"

    //         "LOOPNEZ %[tmp], .Lend_%=" "\n"

    //             "EE.VCMP.EQ.S8 q5, q2, q6" "\n" // == 0?
    //             "EE.VMULAS.S8.ACCX.LD.IP q1, %[str], 16, q5, q5" "\n"

    //             "EE.VCMP.EQ.S8 q5, q2, q7" "\n" // == nl?
    //             "EE.VMULAS.S8.ACCX q5, q5" "\n"

    //             "EE.SRC.Q.QUP q2, q0, q1" "\n"

    //             "RUR.ACCX_0 %[tmp]" "\n"
    //             "BNEZ %[tmp], .Lfound_%=" "\n"

    //         ".Lend_%=:"
    //             "ADDI %[str], %[end], (3*16)" "\n" // Make str point to end
    //         ".Lfound_%=:"
    //             "ADDI %[str], %[str], -(3*16)" "\n"
    //         : "+m" (dummy), 
    //           [str] "+r" (str),
    //           [tmp] "+r" (tmp)
    //         : [end] "r" (end)
    //     );

    //     if(str < end) {
    //         asm (
    //             "LOOPNEZ %[cnt], .Lend_%=" "\n"
    //                 "L8UI %[tmp], %[str], 0" "\n"
    //                 "ADDI %[str], %[str], 1" "\n"
    //                 "BEQI %[tmp], %[nl], .Lfound_%=" "\n"
    //                 "BEQZ %[tmp], .Lfound_%=" "\n"
    //             ".Lend_%=:" "\n"
    //             // "ADDI %[str], %[str], 1" "\n"
    //             ".Lfound_%=:" "\n"
    //             // "ADDI %[str], %[str], -1" "\n"
    //             : [str] "+r" (str), [tmp] "=r" (tmp)
    //             : [nl] "n" ('\n'), [cnt] "r" (end-str)
    //         );
    //         str -= 1;
    //     }
    // }
    // return const_cast<char*>(str);
}