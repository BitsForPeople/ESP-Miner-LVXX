#include "websocket_intf.h"


_Atomic(uint32_t) websocket_client_count;


static StaticEventGroup_t mem {};
EventGroupHandle_t const websocket_event_handle = xEventGroupCreateStatic(&mem);