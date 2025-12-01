#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PTRQUEUE_SIZE 12

typedef struct PtrqueueMem {
    StaticQueue_t sq;
    uint8_t buffer[PTRQUEUE_SIZE*sizeof(void*)];
} PtrqueueMem_t;

static inline QueueHandle_t ptrqueue_init(PtrqueueMem_t* const mem) {
    return xQueueCreateStatic(
        sizeof(mem->buffer)/sizeof(void*),
        sizeof(void*),
        mem->buffer, &mem->sq );
}

static inline bool ptrqueue_enqueue(QueueHandle_t q, void* const ptr, const TickType_t maxWait) {
    return xQueueSendToBack(q,&ptr,maxWait) == pdPASS;
}

static inline bool ptrqueue_dequeue(QueueHandle_t q, void** const out_ptr, const TickType_t maxWait) {
    return xQueueReceive(q,out_ptr,maxWait) == pdPASS;
}

static inline void ptrqueue_consume_all(QueueHandle_t q, void (*fn)(void*)) {
    void* ptr;
    while(ptrqueue_dequeue(q,&ptr,0)) {
        fn(ptr);
    }
}

#ifdef __cplusplus
}
#endif