#pragma once
#include <stdatomic.h>
#include "stratum_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The stratum task exchanges mining notification with the ASIC task via this atomic.
 * 
 */

extern _Atomic(mining_notify*) shared_mining_notify;

static inline void shared_mn_set(mining_notify* const newValue) {
    atomic_store(&shared_mining_notify,newValue);
}

static inline mining_notify* shared_mn_xch(mining_notify* const newValue) {
    return atomic_exchange(&shared_mining_notify,newValue);
}

#ifdef __cplusplus
}
#endif
