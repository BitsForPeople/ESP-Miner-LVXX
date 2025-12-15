#pragma once

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

void json_rpc_parse(const char* str, size_t len);

#ifdef __cplusplus
}
#endif