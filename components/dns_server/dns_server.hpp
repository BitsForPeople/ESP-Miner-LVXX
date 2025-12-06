#include <type_traits>

#include <mutex.hpp>
#include <eventgroup.hpp>
#include <freertos/timers.h>
#include <algorithm>
#include <memory>
#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "dns_server_intf.h"
#include "dns_server_task.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "lwip/sockets.h"

namespace dnss {

    template<typename T>
    class Singleton {
        public:
            template<typename...Args>
            static T* create(Args&&...args) {
                freertos::Lck lck {mux.acquire()};
                if(!inst) {
                    failure = false;
                    inst = new T(std::forward<Args>(args)...);
                    if(failure) {
                        delete inst;
                        inst = nullptr;
                    }
                    return inst;
                } else {
                    return nullptr;
                }
            }

            static bool destroy() {
                return mux.perform([]() {
                    if(inst) {
                        delete inst;
                        return true;
                    } else {
                        return false;
                    }
                });
            }

            static freertos::Lck acquireLock(TickType_t maxWait = portMAX_DELAY) {
                return mux.acquire(maxWait);
            }

            template<typename F, typename ... Args>
            static bool onInstance(F&& op, Args&& ... args) {
                freertos::Lck lck {mux.acquire()};
                if(inst) {
                    std::invoke(op,inst,std::forward<Args>(args)...);
                    return true;
                } else {
                    return false;
                }
            }

        private:

            friend T;
            
            static void creationFailed(void) {
                failure = true;
            } 

            static inline freertos::Mutex mux {};
            static inline T* inst {nullptr};
            static inline bool failure {false};
    };

    class DNSServerInst {
        using sngltn = Singleton<DNSServerInst>;
        friend sngltn;

        static constexpr const char* TAG = "DNSServerInst";
        public:

        bool xchSock(int oldVal, int newVal) {
            return sock.compare_exchange_strong(oldVal,newVal);
        }

        int getSock(void) const {
            return sock.load();
        }

        bool isStopRequested(void) {
            return (evtgrp.getBits() & EB_REQUEST_STOP) != 0;
        }

        void notifyStopped(void) {
            evtgrp.setBits(EB_STOPPED);
        }

        struct dns_server_entry_list getEntryList(void) const {
            struct dns_server_entry_list list {};
            list.size = this->num_entries;
            list.entries = this->entries.get();
            return list;
        }

        ~DNSServerInst(void) {
            doStop();
        }

        private:

            static constexpr EventBits_t EB_REQUEST_STOP = (1<<0);
            static constexpr EventBits_t EB_STOPPED = (1<<1);

            DNSServerInst(const dns_entry_pair_t* const entries, std::size_t num_entries) {

                ESP_LOGD(TAG, "Creating.");
                this->entries = std::make_unique<dns_entry_pair_t[]>(num_entries);
                this->num_entries = num_entries;

                std::copy(entries, entries+num_entries, this->entries.get());
                doStart();
            }

            void doStart(void) {
                this->evtgrp.clearBits(EB_REQUEST_STOP | EB_STOPPED);
                this->sock = 0;
                this->registerEventHandler();
                ESP_LOGD(TAG, "Starting task.");
                if(
                    xTaskCreatePinnedToCore(dns_server_task, "dns_server", 4096, this, 5, &task, xPortGetCoreID())
                    == pdFALSE) {
                        ESP_LOGE(TAG, "Failed to create task!");
                        sngltn::creationFailed();
                    }
            }

            void doStop() {
                ESP_LOGD(TAG, "Stopping.");
                unregisterEventHandler(); 
                // If we got here because of failed task creation, we don't want to wait for a non-existent task to terminate.
                if(this->task) {
                    EventBits_t bits = evtgrp.sync(EB_REQUEST_STOP, EB_STOPPED, 10 / portTICK_PERIOD_MS);
                    if((bits & EB_STOPPED) == 0) {
                        int s = sock.exchange(-1);
                        if(s >= 0) {
                            ESP_LOGD(TAG, "Closing socket.");
                            shutdown(s,0);
                            close(s);
                        }
                        ESP_LOGD(TAG, "Waiting for task to terminate.");
                        evtgrp.waitForAnyBit(EB_STOPPED);
                    }
                }
                ESP_LOGI(TAG, "Stopped.");
            }

        freertos::EventGroup evtgrp {};

        // esp_event_handler_instance_t evtInst {nullptr};

        std::atomic<int> sock {};

        std::size_t num_entries {0};
        std::unique_ptr<dns_entry_pair_t[]> entries {};
        TaskHandle_t task {nullptr};


        void onWifiEvent(int32_t event_id) {
            if(event_id == WIFI_EVENT_AP_STOP) {
                this->doStop();
            }
        }

        esp_err_t registerEventHandler(void) {
            // ESP_LOGI(TAG, "Registering WiFi listener.");
            return esp_event_handler_register(
                WIFI_EVENT, WIFI_EVENT_AP_STOP,
                &DNSServerInst::event_handler_fn,
                nullptr);
        }

        esp_err_t unregisterEventHandler(void) {
            // ESP_LOGI(TAG, "Un-registering WiFi listener.");
            esp_err_t r = ESP_OK;
            // if(evtInst) {
                r = xTimerPendFunctionCall(
                        &DNSServerInst::event_handler_unreg_fn,
                        nullptr,0,
                        portMAX_DELAY);
            //     if(r == ESP_OK) {
            //         evtInst = nullptr;
            //     }
            // }
            return r;
        }

        static void event_handler_fn(void* arg, esp_event_base_t event_base, int32_t event_id, void * event_data) {
            Singleton<DNSServerInst>::onInstance(&DNSServerInst::onWifiEvent, event_id);
            // ((DNSServerInst*)arg)->onWifiEvent(event_id);
        }

        static void event_handler_unreg_fn(void* arg1, uint32_t arg2) {
            esp_event_handler_unregister(WIFI_EVENT,WIFI_EVENT_AP_STOP, &DNSServerInst::event_handler_fn);
        }
    };

}

