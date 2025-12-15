#include "mem_search.h"

// #include <type_traits>
#include <limits>

static constexpr bool known(const bool cond) noexcept {
    return __builtin_constant_p(cond) && cond;
}

static constexpr bool maybe(const bool cond) noexcept {
    return !known(!cond);

}

#if CONFIG_IDF_TARGET_ESP32S3


/* Signed MAC-ing a comparison result with this vector yields a
   value < 0 iff the first 'true' value (-1) is in the 2nd half.
   Any 'true' in the first half contributes 32 to the result, while
   all 'true' values in the second half together contribute at most -8.
   (Because we do _two_ comparisons+MACs per iteration when searching in
   a C string we need 1st half < ((-8)*2).)
*/
alignas(16)
static const int8_t POS_VEC[16] =
    { -32, -32, -32, -32, -32, -32, -32, -32,
        1,   1,   1,   1,   1,   1,   1,   1}; // 1st half values must be < than (2*(-8))x 2nd half values

template<typename T>
static inline void incptr(T*& p, int i) {
    p = (T*)((uintptr_t)p + i);
}

template<typename T, typename U>
static inline int32_t pd(T p1, U p2) {
    return (uintptr_t)p1 - (uintptr_t)p2;
}

static inline constexpr uint32_t min(const uint32_t a, const uint32_t b) {
    return (a<b) ? a : b;
}

template<typename T, typename U>
static inline T* min(T* p1, U p2) {
    return (T*)(min((uintptr_t)p1,(uintptr_t)p2));
}

static inline uintptr_t headroom(const void* mem) {
    uintptr_t m = (uintptr_t)mem;
    return std::numeric_limits<uintptr_t>::max() - m;
}


void* mem_find_u8_(const void* mem, uint32_t maxLen, const uint8_t value) {
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
    return const_cast<void*>(mem);    
}
void* mem_find_u8(const void* mem, uint32_t maxLen, const uint8_t value) {
    // - Why? 
    // - Because this is faster than stdlib... also, because I can...
    // ... allright, I paid for that dang 128-bit data bus so I
    // am going to use it dammit. Stop asking!
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
                ".Lend_%=:" "\n"
                    "ADDI %[mem], %[end], (3*16)" "\n"
                ".Lfound_%=:" "\n"
                    "ADDI %[mem], %[mem], -(3*16)" "\n"
                : [mem] "+r" (mem),
                  [tmp] "+r" (tmp)
                : [end] "r" (end)
            );

            if((int32_t)tmp < 0) {
                // No match in the first half of the vector
                incptr(mem,8);
                mem = min(mem,end);
            }
            // asm ("":"+r" (mem)); // This makes gcc 14.2.0 use a conditional move above instead of a branch.
            // mem = min(mem,end);
        }

        if(pd(end,mem) > 0) {
            uint32_t tmp;
            if(known(value == 0)) {
                asm (
                    "ADDI %[mem], %[mem], -1" "\n"
                    "LOOPNEZ %[cnt], .Lend_%=" "\n"
                        "L8UI %[tmp], %[mem], 1" "\n"
                        "ADDI %[mem], %[mem], 1" "\n"
                        "BEQZ %[tmp], .Lfound_%=" "\n"
                    ".Lend_%=:" "\n"
                    ".Lfound_%=:" "\n"
                    : [mem] "+r" (mem), [tmp] "=r" (tmp)
                    : [cnt] "r" (pd(end,mem))
                );
            } else {
                asm (
                    "ADDI %[mem], %[mem], -1" "\n"
                    "LOOPNEZ %[cnt], .Lend_%=" "\n"
                        "L8UI %[tmp], %[mem], 1" "\n"
                        "ADDI %[mem], %[mem], 1" "\n"
                        "BEQ %[tmp], %[val], .Lfound_%=" "\n"
                    ".Lend_%=:" "\n"
                    ".Lfound_%=:" "\n"
                    : [mem] "+r" (mem), [tmp] "=r" (tmp)
                    : [cnt] "r" (pd(end,mem)),
                      [val] "r" (value)
                );
            }
        }
    }
    return const_cast<void*>(mem);    
}

char* mem_findStrEnd(const char* str, const uint32_t maxLen) {
    return static_cast<char*>(mem_find_u8(str,maxLen,0));
}

char* mem_findInStr(const char* str, const char ch, uint32_t maxLen) {
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

            uint32_t tmp = ((maxLen+15)/16);
            asm (
                "EE.LD.128.USAR.IP q0, %[str], 16" "\n"
                "EE.VLD.128.IP q1, %[str], 16" "\n"

                "EE.ZERO.ACCX" "\n"

                "EE.SRC.Q.QUP q2, q0, q1" "\n"

                "LOOPNEZ %[tmp], .Lend_%=" "\n"

                    "EE.VCMP.EQ.S8 q4, q2, q6" "\n" // q4[n] := q2[n] == 0
                    "EE.VMULAS.S8.ACCX.LD.IP q1, %[str], 16, q4, q5" "\n" // MAC

                    "EE.VCMP.EQ.S8 q4, q2, q7" "\n" // q4[n] := q2[n] == ch
                    "EE.VMULAS.S8.ACCX q4, q5" "\n" // MAC

                    "EE.SRC.Q.QUP q2, q0, q1" "\n"

                    "RUR.ACCX_0 %[tmp]" "\n" // Extract result
                    "BNEZ %[tmp], .Lfound_%=" "\n" // Exit loop if any matches.

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
                str = min(str,end);
            }
            
        }

        if(pd(end,str) > 0) {
            uint32_t tmp;
            asm (
                "ADDI %[str], %[str], -1" "\n"
                "LOOPNEZ %[cnt], .Lend_%=" "\n"
                    "L8UI %[tmp], %[str], 1" "\n"
                    "ADDI %[str], %[str], 1" "\n"
                    "BEQ %[tmp], %[ch], .Lfound_%=" "\n"
                    "BEQZ %[tmp], .Lfound_%=" "\n"
                ".Lend_%=:" "\n"
                ".Lfound_%=:" "\n"
                : [str] "+r" (str), [tmp] "=r" (tmp)
                : [ch] "r" (ch), [cnt] "r" (pd(end,str))
            );
        }
    }
    return const_cast<char*>(str);
}

char* mem_findLineEnd(const char* str, unsigned maxLen) {
    return mem_findInStr(str, '\n', maxLen);
}

#endif // ESP32-S3