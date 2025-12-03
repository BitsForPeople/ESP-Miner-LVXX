#include "tasks_events.h"

static StaticEventGroup_t mem {};

EventGroupHandle_t const tasks_events_handle = xEventGroupCreateStatic(&mem);
