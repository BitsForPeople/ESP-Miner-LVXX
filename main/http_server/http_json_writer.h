#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include "http_writer.h"
#include "statistics_task.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t http_json_write_stats(http_writer_t* const w, const struct StatisticsData* const stats);

// I hate C.
#define http_json_write_item(w,name,value) \
    _Generic((value), \
        uint8_t : http_json_write_int_item, \
        int8_t : http_json_write_int_item, \
        uint16_t : http_json_write_int_item, \
        int16_t : http_json_write_int_item, \
        uint32_t : http_json_write_int_item, \
        int32_t : http_json_write_int_item, \
        unsigned int : http_json_write_int_item, \
        int : http_json_write_int_item, \
        uint64_t : http_json_write_long_int_item, \
        int64_t : http_json_write_long_int_item, \
        float : http_json_write_float_item, \
        double : http_json_write_double_item, \
        char* : http_json_write_str_item, \
        bool : http_json_write_bool_item, \
        const char* : http_json_write_str_item \
    )((w),(name),(value))

#define http_json_write_value(w,value) \
    _Generic((value), \
        uint8_t : http_json_write_int, \
        int8_t : http_json_write_int, \
        uint16_t : http_json_write_int, \
        int16_t : http_json_write_int, \
        uint32_t : http_json_write_int, \
        int32_t : http_json_write_int, \
        unsigned int : http_json_write_int, \
        int : http_json_write_int, \
        uint64_t : http_json_write_long_int, \
        int64_t : http_json_write_long_int, \
        float : http_json_write_float, \
        double : http_json_write_double, \
        char* : http_json_write_str, \
        bool : http_json_write_bool, \
        const char* : http_json_write_str \
    )((w),(value))

esp_err_t http_json_start_obj(http_writer_t* const w, const char* const name);
esp_err_t http_json_end_obj(http_writer_t* const w);

esp_err_t http_json_start_arr(http_writer_t* const w, const char* const name);
esp_err_t http_json_end_arr(http_writer_t* const w);

esp_err_t http_json_write_int_item(http_writer_t* const w, const char* const name, const int32_t value);
esp_err_t http_json_write_long_int_item(http_writer_t* const w, const char* const name, const int64_t value);
esp_err_t http_json_write_float_item(http_writer_t* const w, const char* const name, const float value);
esp_err_t http_json_write_double_item(http_writer_t* const w, const char* const name, const double value);
esp_err_t http_json_write_str_item(http_writer_t* const w, const char* const name, const char* const value);
esp_err_t http_json_write_bool_item(http_writer_t* const w, const char* const name, const bool value);

esp_err_t http_json_write_int(http_writer_t* const w, const int32_t value);
esp_err_t http_json_write_long_int(http_writer_t* const w, const int64_t value);
esp_err_t http_json_write_float(http_writer_t* const w, const float value);
esp_err_t http_json_write_double(http_writer_t* const w, const double value);
esp_err_t http_json_write_bool(http_writer_t* const w, const bool value);

esp_err_t http_json_write_str(http_writer_t* const w, const char* const str);
esp_err_t http_json_write_char(http_writer_t* const w, const char ch);

#ifdef __cplusplus
}
#endif