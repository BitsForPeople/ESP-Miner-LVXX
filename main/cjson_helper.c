#include <string.h>
#include "cjson_helper.h"
#include "cJSON.h"
#include "esp_heap_caps.h"


static void* psram_alloc(size_t sz) {
    return heap_caps_malloc(sz,MALLOC_CAP_SPIRAM);
}

void cjson_use_psram(const bool enable) {
    cJSON_Hooks h;
    memset(&h,0,sizeof(h));
    if(enable) {
        h.malloc_fn = psram_alloc;
    }
    cJSON_InitHooks(&h);
}
