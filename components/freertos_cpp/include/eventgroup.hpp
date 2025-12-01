#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

namespace freertos {
    
    class EventGroup {
        public:
        EventGroup(void) {
            hdl = xEventGroupCreateStatic(&mem);
        }
        EventGroup(const EventGroup&) = delete;
        EventGroup(EventGroup&&) = delete;
        EventGroup& operator =(const EventGroup&) = delete;
        EventGroup& operator =(EventGroup&&) = delete;

        EventBits_t setBits(const EventBits_t bits) {
            return xEventGroupSetBits(hdl,bits);
        }

        EventBits_t clearBits(const EventBits_t bits) {
            return xEventGroupClearBits(hdl,bits);
        }

        EventBits_t getBits(void) {
            return xEventGroupGetBits(hdl);
        }

        EventBits_t waitForBits(const EventBits_t bits, const TickType_t maxWait = portMAX_DELAY) {
            return xEventGroupWaitBits(hdl,bits,bits,1,maxWait);
        }

        EventBits_t waitForAnyBit(const EventBits_t bits, const TickType_t maxWait = portMAX_DELAY) {
            return xEventGroupWaitBits(hdl,bits,bits,0,maxWait);
        }

        private:
        StaticEventGroup_t mem {};
        EventGroupHandle_t hdl;
    };

} // namespace freertos