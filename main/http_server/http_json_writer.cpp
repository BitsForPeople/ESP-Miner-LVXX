#include "http_json_writer.hpp"
#include "http_json_writer.h"

using namespace http;

esp_err_t http_json_write_stats(http_writer_t* const pw, const struct StatisticsData* const stats) {
    json::JsonWriter& w = json::JsonWriter::of(*pw);

    const struct StatisticsData& statsData = *stats;
    w.startArr()
        .writeValue(statsData.hashrate_MHz / 1000.f)
        .writeValue(statsData.chipTemperature)
        .writeValue(statsData.vrTemperature)
        .writeValue(statsData.power)
        .writeValue(statsData.voltage)
        .writeValue(statsData.current)
        .writeValue(statsData.coreVoltageActual)
        .writeValue(statsData.fanSpeed)
        .writeValue(statsData.fanRPM)
        .writeValue(statsData.wifiRSSI)
        .writeValue(statsData.freeHeap)
        .writeValue(statsData.timestamp)
    .endArr();
    return w;

    // return w.writeArr(
    //     statsData.hashrate_MHz / 1000.f,
    //     statsData.chipTemperature,
    //     statsData.vrTemperature,    
    //     statsData.power,
    //     statsData.voltage,
    //     statsData.current,
    //     statsData.coreVoltageActual,
    //     statsData.fanSpeed,
    //     statsData.fanRPM,
    //     statsData.wifiRSSI,
    //     statsData.freeHeap,
    //     statsData.timestamp
    // );
}

esp_err_t http_json_start_obj(http_writer_t* const w, const char* const name) {
    return json::JsonWriter::of(*w).startObj(name).result;
}
esp_err_t http_json_end_obj(http_writer_t* const w) {
    return json::JsonWriter::of(*w).endObj().result;
}

esp_err_t http_json_start_arr(http_writer_t* const w, const char* const name) {
    return json::JsonWriter::of(*w).startArr(name).result;
}
esp_err_t http_json_end_arr(http_writer_t* const w) {
    return json::JsonWriter::of(*w).endArr().result;
}

esp_err_t http_json_write_int_item(http_writer_t* const w, const char* const name, const int32_t value) {
    return json::JsonWriter::of(*w).writeItem(name,value).result;
}
esp_err_t http_json_write_long_int_item(http_writer_t* const w, const char* const name, const int64_t value) {
    return json::JsonWriter::of(*w).writeItem(name,value).result;
}
esp_err_t http_json_write_float_item(http_writer_t* const w, const char* const name, const float value)  {
    return json::JsonWriter::of(*w).writeItem(name,value).result;
}
esp_err_t http_json_write_double_item(http_writer_t* const w, const char* const name, const double value) {
    return json::JsonWriter::of(*w).writeItem(name,value).result;    
}
esp_err_t http_json_write_str_item(http_writer_t* const w, const char* const name, const char* const value)  {
    return json::JsonWriter::of(*w).writeItem(name,value).result;
}
esp_err_t http_json_write_bool_item(http_writer_t* const w, const char* const name, const bool value) {
    return json::JsonWriter::of(*w).writeItem(name,value).result;
}

esp_err_t http_json_write_str(http_writer_t* const w, const char* const str) {
    return json::JsonWriter::of(*w).writeValue(str).result;
}
esp_err_t http_json_write_char(http_writer_t* const w, const char ch) {
    return json::JsonWriter::of(*w).write(ch).result;
}
esp_err_t http_json_write_int(http_writer_t* const w, const int32_t value) {
    return json::JsonWriter::of(*w).writeValue(value).result;
}
esp_err_t http_json_write_long_int(http_writer_t* const w, const int64_t value) {
    return json::JsonWriter::of(*w).writeValue(value).result;
}
esp_err_t http_json_write_float(http_writer_t* const w, const float value) {
    return json::JsonWriter::of(*w).writeValue(value).result;
}
esp_err_t http_json_write_double(http_writer_t* const w, const double value) {
    return json::JsonWriter::of(*w).writeValue(value).result;
}
esp_err_t http_json_write_bool(http_writer_t* const w, const bool value) {
    return json::JsonWriter::of(*w).writeValue(value).result;
}

