#include "asic_task_intf.h"


_Atomic(mining_notify*) asic_task_mining_notify;

static StaticEventGroup_t mem {};

EventGroupHandle_t const asic_task_event_handle = xEventGroupCreateStatic(&mem);
