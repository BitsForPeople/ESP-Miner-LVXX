#include "mem_cpy.h"

#if CONFIG_IDF_TARGET_ESP32S3
    void cpy_mem(const void* src, void* dst, uint32_t cnt) {
        mem_cpy::simdCpy(src,dst,cnt);
    }
#else
    // Definition is in the header file.
#endif