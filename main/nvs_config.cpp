#include "nvs_config.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>
#include <functional>
#include <string_view>
#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

// #define NVS_CONFIG_NAMESPACE "main"
static const char* const NVS_CONFIG_NAMESPACE = "main"; 

static constexpr std::string_view KEY_FREQ {"asicfrequency_f"};

#define FLOAT_STR_LEN 32

static const char * const TAG = "nvs_config";

static constexpr EventBits_t EB_CONFIG_CHANGED = (1<<0);

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

class Lck {
    public:
        static Lck take(SemaphoreHandle_t mux, const TickType_t maxWait = portMAX_DELAY) {
            if(mux) {
                const bool acq = xSemaphoreTake(mux,maxWait) != pdFALSE;
                return Lck {mux,acq};
            } else {
                return Lck {};
            }
        }

        constexpr Lck() = default;
        
        constexpr Lck(SemaphoreHandle_t mux, bool acquired) : mux {mux}, acq {acquired && (mux != nullptr)} {

        }

        constexpr Lck(Lck&& other) : mux {other.mux}, acq {other.acq} {
            other.mux = nullptr;
            other.acq = false;
        }

        constexpr Lck(const Lck&) = delete;

        constexpr Lck& operator =(const Lck&) = delete;
        constexpr Lck& operator =(Lck&& other) {
            this->release();

            this->mux = other.mux;
            this->acq = other.acq;

            other.mux = nullptr;
            other.acq = false;

            return *this;
        }

        constexpr explicit operator bool() const {
            return acquired();
        }

        constexpr bool acquired() const {
            return acq;
        }

        void release() {
            if(acq) {
                if(mux) {
                    xSemaphoreGive(mux);
                }
                this->acq = false;
            }
        }

        ~Lck() {
            if(acq && mux) {
                xSemaphoreGive(mux);
            }
        }
    private:
        SemaphoreHandle_t mux {nullptr};
        bool acq {false};
};

class Mutex {
    public:
        Mutex() {
            handle = xSemaphoreCreateMutexStatic(&mutexMem);        
        }

        Mutex(const Mutex&) = delete;
        Mutex(Mutex&&) = delete;

        SemaphoreHandle_t getHandle(void) const {
            return this->handle;
        }

        operator SemaphoreHandle_t(void) const {
            return getHandle();
        }

        Lck acquire(const TickType_t maxWait = portMAX_DELAY) {
            return Lck::take(handle,maxWait);
        }


        bool take(const TickType_t maxWait = portMAX_DELAY) {
            return xSemaphoreTake(handle,maxWait);
        }

        bool give(void) {
            return xSemaphoreGive(handle);
        }


        bool takeFromISR(void) {
            BaseType_t dummy;
            return xSemaphoreTakeFromISR(handle, &dummy);
        }

        bool giveFromISR(BaseType_t& should_yield) {
            return xSemaphoreGiveFromISR(handle,&should_yield);
        }

    private:
        SemaphoreHandle_t handle {nullptr};
        StaticSemaphore_t mutexMem {};
};

template<typename T, auto R, auto W>
struct NVS_ops {
    static esp_err_t write(nvs_handle_t handle, const char* const key, const T value) {
        return std::invoke(W,handle,key,value);
    }
    static esp_err_t read(nvs_handle_t handle, const char* const key, T& out_value) {
        return std::invoke(R,handle,key,&out_value);
    }
};

template<typename T>
struct NVS_op {

};

template<>
struct NVS_op<uint16_t> : public NVS_ops<uint16_t,nvs_get_u16,nvs_set_u16> {
};

template<>
struct NVS_op<int32_t> : public NVS_ops<int32_t,nvs_get_i32,nvs_set_i32> {

};

template<>
struct NVS_op<uint64_t> : public NVS_ops<uint64_t,nvs_get_u64,nvs_set_u64> {

};

template<>
struct NVS_op<char*> {

    static esp_err_t write(nvs_handle_t handle, const char* const key, const char* value) {
        return nvs_set_str(handle,key,value);
    }
    static esp_err_t read(nvs_handle_t handle, const char* const key, char* const out_value, size_t& size)
    {
        return nvs_get_str(handle, key, out_value, &size);
    }
};

template<>
struct NVS_op<float> {
    static esp_err_t write(nvs_handle_t handle, const char* const key, const float value) {
        char str_value[FLOAT_STR_LEN];
        snprintf(str_value, sizeof(str_value), "%.6f", value);
        return NVS_op<char*>::write(handle,key,str_value);
    }
    static esp_err_t read(nvs_handle_t handle, const char* const key, float& out_value) {

        // const std::string_view k {key};
        // const bool freqKey = k == KEY_FREQ;

        // if(freqKey) {
        //     ESP_LOGE(TAG, "Read asic freq.");
        // }

        char str_value[FLOAT_STR_LEN] = {};
        size_t sz = sizeof(str_value)-1;

        esp_err_t r = NVS_op<char*>::read(handle,key,str_value,sz);

        // if(freqKey) {
        //     ESP_LOGE(TAG, "Err: %d, sz: %" PRIu32,r,(uint32_t)sz);
        //     if(r == ESP_OK) {
        //         ESP_LOGE(TAG, "value: \"%s\"",str_value);
        //     }
        // }

        if(r == ESP_OK) {
            if(sz != 0) {
                char *endptr;
                float value = strtof(str_value, &endptr);
                if (endptr == str_value || *endptr != '\0') {
                    ESP_LOGW(TAG, "Invalid float format for key %s: %s", key, str_value);
                    return ESP_FAIL;
                }
                out_value = value;
            } else {
                r = ESP_ERR_NOT_FOUND;
            }
        }
        return r;
    }
};



class NvsCtx {
    public:

    NvsCtx() {

    }

    NvsCtx(NvsCtx&&) = delete;
    NvsCtx(const NvsCtx&) = delete;

    Lck acquire(const TickType_t maxWait = portMAX_DELAY) {
        return mutex.acquire(maxWait);
    }

    esp_err_t commit() const {
        if(handle) {
            return nvs_commit(handle);
        } else {
            return ESP_ERR_INVALID_STATE;
        }
    }

    void close() {
        if(handle) {
            nvs_close(handle);
            handle = 0;
        }
    }

    nvs_handle_t getHandle() {
        if(!this->handle) {
            esp_err_t r = nvs_open(NVS_CONFIG_NAMESPACE,NVS_READWRITE,&this->handle);
            if(r != ESP_OK) {
                ESP_LOGE(TAG, "Failed to open NVS: %d", r);
                return 0;
            }
        }
        return this->handle;
    }



    esp_err_t set_str(const char* const key, const char* const value) {
        return writeNvs<nvs_set_str>(key,value);
    }

    template<typename T>
    T get(const char* const key, const T default_value) {
        return doRead<NVS_op<T>::read>(key,default_value);
    }

    template<typename T>
    esp_err_t set(const char* const key, const T value) {
        return writeNvs<NVS_op<T>::write>(key,value);
    }

    uint32_t getModCnt(void) const {
        return this->modCnt.load(std::memory_order::relaxed);
    }

    bool waitForModification(const TickType_t maxWait) {
        return egrp.waitForAnyBit(EB_CONFIG_CHANGED) == EB_CONFIG_CHANGED;
    }

    // esp_err_t set_u16(const char* const key, const uint16_t value) {
    //     return writeNvs<nvs_set_u16>(key,value);
    // }

    // esp_err_t set_i32(const char* const key, const int32_t value) {
    //     return writeNvs<nvs_set_i32>(key,value);
    // }

    // esp_err_t set_u64(const char* const key, const uint64_t value) {
    //     return writeNvs<nvs_set_u64>(key,value);
    // }

    // esp_err_t set_float(const char* const key, const float value) {
    //     char str_value[FLOAT_STR_LEN];
    //     snprintf(str_value, sizeof(str_value), "%.6f", value);
    //     return set_str(key,str_value);
    // }

    private:

    Mutex mutex {};
    nvs_handle_t handle {0};
    EventGroup egrp {};
    std::atomic<uint32_t> modCnt {0};

    template<auto F, typename T>
    esp_err_t writeNvs(const char* const key, T value) {
        Lck lck {acquire()};
        const esp_err_t r = std::invoke(F, getHandle(), key, value);
        if(r == ESP_OK) {
            commit();

            // Take note of the potential change in config data:
            this->modCnt.fetch_add(1,std::memory_order::relaxed);
            // And notify any listening tasks:
            egrp.setBits(EB_CONFIG_CHANGED);        

        } else {
            ESP_LOGW(TAG, "Failed to write NVS key \"%s\": %d",key,r);
        }

        return r;   
    }

    template<auto F, typename T>
    T doRead(const char* const key, const T default_value) {
        T val;
        esp_err_t r = execRead<F>(key,val);
        if(r == ESP_OK) {
            return val;
        } else {
            return default_value;
        }
    }

    template<auto F, typename ... Args>
    requires requires (const nvs_handle_t h, Args...args) {{std::invoke(F,h,args...)};}
    auto execRead(Args&&...args) {
        Lck lck {acquire()};
        return std::invoke(F,getHandle(), std::forward<Args>(args)...);
    }

};

static NvsCtx ctx {};

uint32_t nvs_config_get_modcount(void) {
    return ctx.getModCnt();
}

bool nvs_config_wait_for_modification(TickType_t maxWait) {
    return ctx.waitForModification(maxWait);
}

static char * do_nvs_config_get_string(nvs_handle_t handle, const char * key, const char * default_value)
{
    // Lck lck = ctx.acquire();
    // if(lck) {
    //     nvs_handle_t handle = ctx.getHandle();
    //     if(handle) {
    //         size_t size = 0;
    //         esp_err_t r = nvs_get_str(handle, key, NULL, &size);
    //         if(r == ESP_OK) {
    //             char * out = (char*)malloc(size);
    //             if(out) {
    //                 r = nvs_get_str(handle, key, out, &size);
    //                 if(r == ESP_OK) {
    //                     return out;
    //                 }
    //             }
    //         }
    //     }
    // }
    // if(default_value) {
    //     return strdup(default_value);
    // } else {
    //     return nullptr;
    // }

    // Lck lck {ctx.acquire()};
    // nvs_handle_t handle = ctx.getHandle();

    // nvs_handle handle;
    esp_err_t err;
    // err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    // if (err != ESP_OK) {
    //     return strdup(default_value);
    // }

    size_t size = 0;
    err = nvs_get_str(handle, key, NULL, &size);

    if (err != ESP_OK) {
        // nvs_close(handle);
        return strdup(default_value);
    }

    char * out = (char*)malloc(size);
    err = nvs_get_str(handle, key, out, &size);

    if (err != ESP_OK) {
        free(out);
        // nvs_close(handle);
        return strdup(default_value);
    }

    // nvs_close(handle);
    return out;
}

char * nvs_config_get_string(const char * key, const char * default_value) {
    Lck lck {ctx.acquire()};
    // return ctx.execRead<do_nvs_config_get_string>(key,default_value);
    return do_nvs_config_get_string(ctx.getHandle(),key,default_value);
}

// static void do_nvs_config_set_string(nvs_handle_t handle, const char * key, const char * value)
// {
//     // nvs_handle handle;
//     esp_err_t err;
//     // err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
//     // if (err != ESP_OK) {
//     //     ESP_LOGW(TAG, "Could not open nvs");
//     //     return;
//     // }

//     err = nvs_set_str(handle, key, value);
//     if (err != ESP_OK) {
//         ESP_LOGW(TAG, "Could not write nvs key: %s, value: %s", key, value);
//     }

//     // nvs_close(handle);
//     nvs_commit(handle);
// }

void nvs_config_set_string(const char * key, const char * value) {
    ctx.set_str(key,value);
}

// static uint16_t do_nvs_config_get_u16(nvs_handle_t handle, const char * key, const uint16_t default_value)
// {
//     // nvs_handle handle;
//     esp_err_t err;
//     // err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
//     // if (err != ESP_OK) {
//     //     return default_value;
//     // }

//     uint16_t out;
//     err = nvs_get_u16(handle, key, &out);
//     // nvs_close(handle);

//     if (err != ESP_OK) {
//         return default_value;
//     }
//     return out;
// }

uint16_t nvs_config_get_u16(const char * key, const uint16_t default_value) {
    // return ctx.execRead<do_nvs_config_get_u16>(key,default_value);
    return ctx.get<uint16_t>(key,default_value);
}

void nvs_config_set_u16(const char * key, const uint16_t value)
{
    // ctx.set_u16(key,value);
    ctx.set<uint16_t>(key,value);
    // nvs_handle handle;
    // esp_err_t err;
    // err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    // if (err != ESP_OK) {
    //     ESP_LOGW(TAG, "Could not open nvs");
    //     return;
    // }

    // err = nvs_set_u16(handle, key, value);
    // if (err != ESP_OK) {
    //     ESP_LOGW(TAG, "Could not write nvs key: %s, value: %u", key, value);
    // }

    // nvs_close(handle);
}

int32_t nvs_config_get_i32(const char * key, const int32_t default_value)
{
    return ctx.get<int32_t>(key, default_value);
    // nvs_handle handle;
    // esp_err_t err;
    // err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    // if (err != ESP_OK) {
    //     return default_value;
    // }

    // int32_t out;
    // err = nvs_get_i32(handle, key, &out);
    // nvs_close(handle);

    // if (err != ESP_OK) {
    //     return default_value;
    // }
    // return out;
}

void nvs_config_set_i32(const char * key, const int32_t value)
{
    ctx.set<int32_t>(key,value);
    // nvs_handle handle;
    // esp_err_t err;
    // err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    // if (err != ESP_OK) {
    //     ESP_LOGW(TAG, "Could not open nvs");
    //     return;
    // }

    // err = nvs_set_i32(handle, key, value);
    // if (err != ESP_OK) {
    //     ESP_LOGW(TAG, "Could not write nvs key: %s, value: %li", key, value);
    // }

    // nvs_close(handle);
}

uint64_t nvs_config_get_u64(const char * key, const uint64_t default_value)
{
    return ctx.get<uint64_t>(key,default_value);
    // nvs_handle handle;
    // esp_err_t err;
    // err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    // if (err != ESP_OK) {
    //     return default_value;
    // }

    // uint64_t out;
    // err = nvs_get_u64(handle, key, &out);

    // if (err != ESP_OK) {
    //     nvs_close(handle);
    //     return default_value;
    // }

    // nvs_close(handle);
    // return out;
}

void nvs_config_set_u64(const char * key, const uint64_t value)
{
    ctx.set<uint64_t>(key,value);
    // nvs_handle handle;
    // esp_err_t err;
    // err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    // if (err != ESP_OK) {
    //     ESP_LOGW(TAG, "Could not open nvs");
    //     return;
    // }

    // err = nvs_set_u64(handle, key, value);
    // if (err != ESP_OK) {
    //     ESP_LOGW(TAG, "Could not write nvs key: %s, value: %llu", key, value);
    // }
    // nvs_close(handle);
}

float nvs_config_get_float(const char *key, float default_value)
{
    return ctx.get<float>(key,default_value);
    // char default_str[FLOAT_STR_LEN];
    // snprintf(default_str, sizeof(default_str), "%.6f", default_value);

    // char *str_value = nvs_config_get_string(key, default_str);

    // char *endptr;
    // float value = strtof(str_value, &endptr);
    // if (endptr == str_value || *endptr != '\0') {
    //     ESP_LOGW(TAG, "Invalid float format for key %s: %s", key, str_value);
    //     value = default_value;
    // }

    // free(str_value);
    // return value;
}

void nvs_config_set_float(const char *key, float value)
{
    // char str_value[FLOAT_STR_LEN];
    // snprintf(str_value, sizeof(str_value), "%.6f", value);

    // nvs_config_set_string(key, str_value);
    ctx.set<float>(key,value);
}

void nvs_config_commit()
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open nvs");
        return;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not commit nvs");
    }
    nvs_close(handle);
}
