#include "tasks_events.h"

StaticEventGroup_t mem;

EventGroupHandle_t tasks_events_handle {};

void tasks_events_init(void) {
    if(!tasks_events_handle) {
        tasks_events_handle = xEventGroupCreateStatic(&mem);
    }
}