#pragma once

#include "dns_server.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct WifiEventListenerCtx {
    dns_server_handle_t dnsHdl;
} WifiEventListenerCtx_t;

bool wifi_event_listener_start(WifiEventListenerCtx_t* const ctx);
void wifi_event_listener_stop(void);

#ifdef __cplusplus
}
#endif