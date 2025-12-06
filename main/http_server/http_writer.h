#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <esp_http_server.h>
#include <esp_err.h>

#define HTTP_WRITER_BUF_SIZE    128

#ifdef __cplusplus
extern "C" {
#endif

typedef struct http_writer {
    httpd_req_t* req;
    esp_err_t result;
    size_t used;
    bool cont; // used by json writer only.
    uint8_t buf[HTTP_WRITER_BUF_SIZE];
} http_writer_t;

static inline void http_writer_init(http_writer_t* w, httpd_req_t* req) {
    w->req = req;
    w->result = ESP_OK;
    w->used = 0;
    w->cont = false;
}

esp_err_t http_writer_write_int(http_writer_t* w, int64_t value);
esp_err_t http_writer_write_float(http_writer_t* w, float value);
esp_err_t http_writer_write_data(http_writer_t* w, const void* data, size_t len);
esp_err_t http_writer_write_str(http_writer_t* w, const char* str);
esp_err_t http_writer_write_char(http_writer_t* w, const char ch);
esp_err_t http_writer_finish(http_writer_t* w);

// esp_err_t http_writer_write_stats(http_writer_t* const w, const struct StatisticsData* const stats);

#ifdef __cplusplus
}
#endif
