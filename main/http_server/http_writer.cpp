#include "http_writer.hpp"
#include "http_writer.h"
#include "statistics_task.h"

using namespace http;

esp_err_t http_writer_write_int(http_writer_t* w, int64_t value) {
    return Writer::of(*w).write(value).result;
}

esp_err_t http_writer_write_float(http_writer_t* w, float value) {
    return Writer::of(*w).write(value).result;
}

esp_err_t http_writer_write_data(http_writer_t* w, const void* data, size_t len) {
    return Writer::of(*w).write(data,len).result;
}

esp_err_t http_writer_write_str(http_writer_t* w, const char* str) {
    return Writer::of(*w).write(str).result;
}

esp_err_t http_writer_write_char(http_writer_t* w, const char ch) {
    return Writer::of(*w).write(ch).result;
}

esp_err_t http_writer_finish(http_writer_t* w) {
    return Writer::of(*w).finish().result;
}
