#include "nvs_config.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>
#include <functional>
#include <string_view>
#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "mutex.hpp"
#include "eventgroup.hpp"

static const char* const NVS_CONFIG_NAMESPACE = "main"; 

static constexpr std::size_t FLOAT_STR_LEN = 32;

static const char * const TAG = "nvs_config";

static constexpr EventBits_t EB_CONFIG_CHANGED = (1<<0);


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

        char str_value[FLOAT_STR_LEN] = {};
        size_t sz = sizeof(str_value)-1;

        esp_err_t r = NVS_op<char*>::read(handle,key,str_value,sz);

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

    NvsCtx() = default;

    NvsCtx(NvsCtx&&) = delete;
    NvsCtx(const NvsCtx&) = delete;

    [[nodiscard]]
    freertos::Lck acquire(const TickType_t maxWait = portMAX_DELAY) {
        return mutex.acquire(maxWait);
    }

    esp_err_t commit(void) {
        freertos::Lck lck {acquire()};
        return doCommit();
    }

    void close(void) {
        freertos::Lck lck {acquire()};
        doClose();
    }

    nvs_handle_t getHandle(void) {
        freertos::Lck lck {acquire()};
        return doGetHandle();
    }

    esp_err_t set_str(const char* const key, const char* const value) {
        return writeNvs<nvs_set_str>(key,value);
    }

    template<typename T>
    T get(const char* const key, const T default_value) {
        return readNvs<NVS_op<T>::read>(key,default_value);
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

    template<auto F, typename ... Args>
    requires requires (const nvs_handle_t h, Args...args) {{std::invoke(F,h,args...)};}
    auto execRead(Args&&...args) {
        freertos::Lck lck {acquire()};
        return std::invoke(F,doGetHandle(), std::forward<Args>(args)...);
    }

    private:

    freertos::Mutex mutex {};
    nvs_handle_t handle {0};
    freertos::EventGroup egrp {};
    std::atomic<uint32_t> modCnt {0};


    nvs_handle_t doGetHandle(void) {
        if(!this->handle) {
            esp_err_t r = nvs_open(NVS_CONFIG_NAMESPACE,NVS_READWRITE,&this->handle);
            if(r != ESP_OK) {
                ESP_LOGE(TAG, "Failed to open NVS: %d", r);
                return 0;
            }
        }
        return this->handle;
    }

    void doClose(void) {
        if(handle) {
            nvs_close(handle);
            handle = 0;
        }
    }

    esp_err_t doCommit(void) const {
        if(handle) {
            return nvs_commit(handle);
        } else {
            return ESP_ERR_INVALID_STATE;
        }
    }

    template<auto F, typename T>
    esp_err_t writeNvs(const char* const key, T value) {
        freertos::Lck lck {acquire()};
        const esp_err_t r = std::invoke(F, doGetHandle(), key, value);
        if(r == ESP_OK) {
            doCommit();
            // Take note of the potential change in config data:
            modCnt.fetch_add(1,std::memory_order::relaxed);
            // And notify any listening tasks:
            egrp.setBits(EB_CONFIG_CHANGED);        

        } else {
            ESP_LOGW(TAG, "Failed to write NVS key \"%s\": %d",key,r);
        }

        return r;   
    }

    template<auto F, typename T>
    T readNvs(const char* const key, const T default_value) {
        T val;
        esp_err_t r = execRead<F>(key,val);
        if(r == ESP_OK) {
            return val;
        } else {
            return default_value;
        }
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
    esp_err_t err;
    size_t size = 0;
    err = nvs_get_str(handle, key, NULL, &size);

    if (err != ESP_OK) {
        return strdup(default_value);
    }

    char * out = (char*)malloc(size);
    err = nvs_get_str(handle, key, out, &size);

    if (err != ESP_OK) {
        free(out);
        return strdup(default_value);
    }

    return out;
}

char * nvs_config_get_string(const char * key, const char * default_value) {
    return ctx.execRead<do_nvs_config_get_string>(key,default_value);
}


void nvs_config_set_string(const char * key, const char * value) {
    ctx.set_str(key,value);
}

uint16_t nvs_config_get_u16(const char * key, const uint16_t default_value) {
    return ctx.get<uint16_t>(key,default_value);
}

void nvs_config_set_u16(const char * key, const uint16_t value)
{
    ctx.set<uint16_t>(key,value);
}

int32_t nvs_config_get_i32(const char * key, const int32_t default_value)
{
    return ctx.get<int32_t>(key, default_value);
}

void nvs_config_set_i32(const char * key, const int32_t value)
{
    ctx.set<int32_t>(key,value);
}

uint64_t nvs_config_get_u64(const char * key, const uint64_t default_value)
{
    return ctx.get<uint64_t>(key,default_value);
}

void nvs_config_set_u64(const char * key, const uint64_t value)
{
    ctx.set<uint64_t>(key,value);
}

float nvs_config_get_float(const char *key, float default_value)
{
    return ctx.get<float>(key,default_value);
}

void nvs_config_set_float(const char *key, float value)
{
    ctx.set<float>(key,value);
}
