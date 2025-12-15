#pragma once

#include <stdint.h>
#include <sdkconfig.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_IDF_TARGET_ESP32S3
void* mem_find_u8(const void* mem, uint32_t maxLen, const uint8_t value);
char* mem_findStrEnd(const char* str, uint32_t maxLen);
char* mem_findLineEnd(const char* str, uint32_t maxLen);

#else

#include <string.h>

static inline void* mem_find_u8(const void* mem, const uint32_t maxLen, const uint8_t value) {
    void* const p = memchr(mem,value,maxLen);
    if(p != NULL) {
        return p;
    } else {
        return (uint8_t*)mem + maxLen;
    }
}

static inline char* mem_findStrEnd(const char* str, const uint32_t maxLen) {
    return (char*)mem_find_u8(str,maxLen,0);
}

static inline char* mem_findLineEnd(const char* str, uint32_t maxLen) {
    static const char* const memEnd = (const char*)-1;

    const char* ptr = str;
    const char* end = str+maxLen;
    if(end < str) {
        end = memEnd;
    }
    while(ptr < end) {
        if(*ptr == '\n') {
            goto found;
        }
        if(*ptr == '\0') {
            goto found;
        }
        ++ptr;
    }
    found:
    return (char*)ptr;
}

#endif // !ESP32-S3

static inline uint32_t mem_strlen(const char* const str) {
    return (const char*)mem_findStrEnd(str,0x7ffffffful) - str;
}

#ifdef __cplusplus
}
#endif