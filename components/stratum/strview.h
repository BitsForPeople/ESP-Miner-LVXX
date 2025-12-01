#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StrView {
    const char* str;
    size_t len;
} StrView_t;


#ifdef __cplusplus
}
#endif