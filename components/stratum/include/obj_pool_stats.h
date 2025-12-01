#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ObjPoolStats {
    uint32_t allocCnt;
    uint32_t inUseCnt;
    uint32_t maxInUseCnt;
} ObjPoolStats_t;

#ifdef __cplusplus
}
#endif