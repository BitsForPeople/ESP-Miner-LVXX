#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*asic_rst_fun_t)(bool en);

typedef struct ASIC_ctrl_cfg {
    asic_rst_fun_t reset_fn;
} ASIC_ctrl_cfg_t;


#ifdef __cplusplus
}
#endif