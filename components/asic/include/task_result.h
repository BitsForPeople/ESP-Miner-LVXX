#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __attribute__((__packed__))
{
    uint8_t job_id;
    uint32_t nonce;
    uint32_t rolled_version;
} task_result;

#ifdef __cplusplus
}
#endif