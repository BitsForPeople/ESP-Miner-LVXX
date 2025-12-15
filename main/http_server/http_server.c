#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_vfs.h"
#include "esp_heap_caps.h"

#include "dns_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/inet.h"
#include "lwip/lwip_napt.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#include "cJSON.h"
#include "global_state.h"
#include "nvs_config.h"
#include "vcore.h"
#include "power.h"
#include "connect.h"
#include "asic.h"
#include "TPS546.h"
#include "statistics_task.h"
#include "theme_api.h"  // Add theme API include
#include "axe-os/api/system/asic_settings.h"
#include "http_server.h"
#include "system.h"
#include "websocket.h"

#include "http_writer.h"
#include "http_json_writer.h"

// #include "wifi_event_listener.h"

#define JSON_ALL_STATS_ELEMENT_SIZE 120
#define JSON_DASHBOARD_STATS_ELEMENT_SIZE 60

static const char * const TAG = "http_server";
static const char * const CORS_TAG = "CORS";

static const esp_vfs_spiffs_conf_t SPIFFS_CONF = {.base_path = "", .partition_label = NULL, .max_files = 5, .format_if_mount_failed = false};


typedef struct MimeMapping {
    const char* ext;
    const char* type;
} MimeMapping_t;

static const MimeMapping_t MIMES[] = {
    {.ext = "html", .type = "text/html"},
    {.ext = "js"  , .type = "application/javascript"},
    {.ext = "css",  .type = "text/css"},
    {.ext = "png",  .type = "image/png"},
    {.ext = "ico",  .type = "image/x-icon"},
    {.ext = "svg",  .type = "image/svg+xml"},
    {.ext = "pdf",  .type = "application/pdf"},
    {.ext = "woff2", .type = "font/woff2"}
};

static const unsigned NUM_MIMES = sizeof(MIMES) / sizeof(MIMES[0]);


static char axeOSVersion[32];

static httpd_handle_t server = NULL;


static inline void* allocPrefPSRAM(size_t sz) {
    void* m = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(m == NULL) {
        m = heap_caps_malloc(sz, MALLOC_CAP_8BIT);
    }
    return m;
}

/* Handler for WiFi scan endpoint */
static esp_err_t GET_wifi_scan(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    
    // Give some time for the connected flag to take effect
    vTaskDelay(100 / portTICK_PERIOD_MS);
    
    wifi_ap_record_simple_t ap_records[16];
    const unsigned maxCnt = sizeof(ap_records)/sizeof(ap_records[0]); 
    uint16_t ap_count = maxCnt;

    esp_err_t err = wifi_scan(ap_records, &ap_count);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WiFi scan failed");
        return ESP_OK;
    }
    const unsigned cnt = ap_count < maxCnt ? ap_count : maxCnt;

    http_writer_t wrtr;
    http_writer_t* const w = &wrtr;
    http_writer_init(w,req);

    http_json_start_obj(w,NULL);
    http_json_start_arr(w, "networks");

    for (unsigned i = 0; i < cnt; i++) {
        http_json_start_obj(w,NULL);
            http_json_write_item(w, "ssid", ap_records[i].ssid);
            http_json_write_item(w, "rssi", ap_records[i].rssi);
            http_json_write_item(w, "authmode", (int)(ap_records[i].authmode));
        http_json_end_obj(w);
    }

    http_json_end_arr(w);
    http_json_end_obj(w);

    return http_writer_finish(w);

}


#define REST_CHECK(a, str, goto_tag, ...)                                                                                          \
    do {                                                                                                                           \
        if (!(a)) {                                                                                                                \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__);                                                  \
            goto goto_tag;                                                                                                         \
        }                                                                                                                          \
    } while (0)

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)
// #define MESSAGE_QUEUE_SIZE (128)

typedef struct rest_server_context
{
    // char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

static esp_err_t ip_in_private_range(uint32_t address) {
    uint32_t ip_address = ntohl(address);

    // 10.0.0.0 - 10.255.255.255 (Class A)
    if ((ip_address >= 0x0A000000) && (ip_address <= 0x0AFFFFFF)) {
        return ESP_OK;
    }

    // 172.16.0.0 - 172.31.255.255 (Class B)
    if ((ip_address >= 0xAC100000) && (ip_address <= 0xAC1FFFFF)) {
        return ESP_OK;
    }

    // 192.168.0.0 - 192.168.255.255 (Class C)
    if ((ip_address >= 0xC0A80000) && (ip_address <= 0xC0A8FFFF)) {
        return ESP_OK;
    }

    return ESP_FAIL;
}

static uint32_t extract_origin_ip_addr(char *origin)
{
    static const char PREFIX[] = "http://";
    static const size_t PREFIX_LEN = sizeof(PREFIX)-1;

    char ip_str[16];
    uint32_t origin_ip_addr = 0;

    // Find the start of the IP address in the Origin header
    // const char *prefix = "http://";
    if(strncmp(origin,PREFIX,PREFIX_LEN) == 0) {
        const char* const ip_start = origin + PREFIX_LEN; // strstr(origin, prefix);
    // if (ip_start) {
    //     ip_start += strlen(prefix); // Move past "http://"

        // Extract the IP address portion (up to the next '/')
        const char* ip_end = strchr(ip_start, '/');
        size_t ip_len = ip_end ? (size_t)(ip_end - ip_start) : strlen(ip_start);
        if (ip_len < sizeof(ip_str)) {
            strncpy(ip_str, ip_start, ip_len);
            ip_str[ip_len] = '\0'; // Null-terminate the string

            // Convert the IP address string to uint32_t
            origin_ip_addr = inet_addr(ip_str);
            if (origin_ip_addr == INADDR_NONE) {
                ESP_LOGW(CORS_TAG, "Invalid IP address: %s", ip_str);
            } else {
                ESP_LOGD(CORS_TAG, "Extracted IP address %lu", origin_ip_addr);
            }
        } else {
            ESP_LOGW(CORS_TAG, "IP address string is too long: %s", ip_start);
        }
    }

    return origin_ip_addr;
}

esp_err_t is_network_allowed(httpd_req_t * req)
{
    if (GLOBAL_STATE.SYSTEM_MODULE.ap_enabled == true) {
        ESP_LOGI(CORS_TAG, "Device in AP mode. Allowing CORS.");
        return ESP_OK;
    }

    int sockfd = httpd_req_to_sockfd(req);
    char ipstr[INET6_ADDRSTRLEN];
    struct sockaddr_in6 addr;   // esp_http_server uses IPv6 addressing
    socklen_t addr_size = sizeof(addr);

    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_size) < 0) {
        ESP_LOGE(CORS_TAG, "Error getting client IP");
        return ESP_FAIL;
    }

    uint32_t request_ip_addr = addr.sin6_addr.un.u32_addr[3];

    // // Convert to IPv6 string
    // inet_ntop(AF_INET, &addr.sin6_addr, ipstr, sizeof(ipstr));

    // Convert to IPv4 string
    inet_ntop(AF_INET, &request_ip_addr, ipstr, sizeof(ipstr));

    // Attempt to get the Origin header.
    uint32_t origin_ip_addr;
    {
        char origin[128];
        if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK) {
            ESP_LOGD(CORS_TAG, "Origin header: %s", origin);
            origin_ip_addr = extract_origin_ip_addr(origin);
        } else {
            ESP_LOGD(CORS_TAG, "No origin header found.");
            origin_ip_addr = request_ip_addr;
        }
    }

    if (ip_in_private_range(origin_ip_addr) == ESP_OK && ip_in_private_range(request_ip_addr) == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGI(CORS_TAG, "Client is NOT in the private ip ranges or same range as server.");
    return ESP_FAIL;
}

static void readAxeOSVersion(void) {
    FILE* f = fopen("/version.txt", "r");
    if (f != NULL) {
        size_t n = fread(axeOSVersion, 1, sizeof(axeOSVersion) - 1, f);
        axeOSVersion[n] = '\0';
        fclose(f);

        ESP_LOGI(TAG, "AxeOS version: %s", axeOSVersion);

        if (strcmp(axeOSVersion, esp_app_get_description()->version) != 0) {
            ESP_LOGW(TAG, "Firmware (%s) and AxeOS (%s) versions do not match. Please make sure to update both www.bin and esp-miner.bin.", esp_app_get_description()->version, axeOSVersion);
        }
    } else {
        strcpy(axeOSVersion, "unknown");
        ESP_LOGI(TAG, "Failed to open AxeOS version.txt");
    }
}


esp_err_t init_fs(void)
{
    esp_err_t ret = esp_vfs_spiffs_register(&SPIFFS_CONF);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    readAxeOSVersion();

    return ESP_OK;
}

/* Function for stopping the webserver */
void stop_webserver(httpd_handle_t server)
{
    if (server) {
        /* Stop the httpd server */
        httpd_stop(server);
    }
}

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t * req, const char * filepath)
{
    const char * type = "text/plain";
    // Get a pointer to the extension part of the filepath, i.e. the last '.' in the string.
    const char* ext = strrchr(filepath,'.');
    if(ext) {
        ext += 1; // skip the '.'
        if(*ext != '\0') {
            for( unsigned i = 0; i < NUM_MIMES; ++i ) {
                if(strcasecmp(ext,MIMES[i].ext) == 0) {
                    type = MIMES[i].type;
                    break;
                }
            }
        }
    }
    return httpd_resp_set_type(req, type);
}

esp_err_t set_cors_headers(httpd_req_t * req)
{
    esp_err_t err;

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* Recovery handler */
static esp_err_t rest_recovery_handler(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    extern const unsigned char recovery_page_start[] asm("_binary_recovery_page_html_start");
    extern const unsigned char recovery_page_end[] asm("_binary_recovery_page_html_end");
    const size_t recovery_page_size = (recovery_page_end - recovery_page_start);
    httpd_resp_send_chunk(req, (const char*)recovery_page_start, recovery_page_size);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Send a 404 as JSON for unhandled api routes */
static esp_err_t rest_api_common_handler(httpd_req_t * req)
{
    static const char ERR[] = "{\"error\":\"unknown route\"}";

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // cJSON * root = cJSON_CreateObject();
    // cJSON_AddStringToObject(root, "error", "unknown route");

    // const char * error_obj = cJSON_Print(root);
    httpd_resp_set_status(req, HTTPD_404);
    httpd_resp_send(req,ERR,sizeof(ERR)-1);
    // free((char *)error_obj);
    // cJSON_Delete(root);
    return ESP_OK;
}

// static bool file_exists(const char *path) {
//     struct stat buffer;
//     return (stat(path, &buffer) == 0);
// }

static esp_err_t redirectToRoot(httpd_req_t* const req) {
    static const char RSP[] = "Redirecting.";

    ESP_LOGI(TAG, "Redirecting to root");

    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    return httpd_resp_send(req, RSP, sizeof(RSP)-1);
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t file_serve_handler(httpd_req_t * req)
{
    char filepath[FILE_PATH_MAX];
    // char gz_file[FILE_PATH_MAX];
    const size_t MAX_FP = sizeof(filepath)-3-1; // leave room for ".gz\0" at the end.
    filepath[MAX_FP] = '\0';

    // Build the file path to use from the base_path + the requested path (+ "index.html")
    rest_server_context_t* const rest_context = (rest_server_context_t *) req->user_ctx;

    size_t fplen = 0;
    if(SPIFFS_CONF.base_path[0] != '\0') {
        fplen = strlcpy(filepath, SPIFFS_CONF.base_path, MAX_FP);
    }
    if(fplen < MAX_FP) {
        fplen += strlcpy(filepath + fplen, req->uri, MAX_FP - fplen);
    }

    if(fplen >= MAX_FP) {
        return redirectToRoot(req);
    }

    const bool uri_is_dir = filepath[fplen-1] == '/';
    //req->uri[strlen(req->uri) - 1] == '/';
    if (uri_is_dir) {
        fplen += strlcpy(filepath + fplen, "index.html", MAX_FP - fplen);
        // strlcat(filepath, "/index.html", filePathLength);
        if(fplen >= MAX_FP) {
            return redirectToRoot(req);
        }
    } 

    // Append ".gz\0" to the filepath.
    filepath[fplen+0] = '.';
    filepath[fplen+1] = 'g';
    filepath[fplen+2] = 'z';
    filepath[fplen+3] = '\0';

    // Try opening <filepath>.gz first:
    bool serve_gz = true;
    int fd = open(filepath, O_RDONLY, 0);

    filepath[fplen] = '\0'; // cut off ".gz" again.

    if(fd == -1) {
        // <filepath>.gz not found. Try again with the original <filepath>.
        serve_gz = false;
        fd = open(filepath, O_RDONLY, 0);
    }

    if (fd == -1) {
        ESP_LOGI(TAG, "File not found: \"%s\"",filepath);
        return redirectToRoot(req);
    }

    if (!uri_is_dir) {
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=2592000");
    }

    if (serve_gz) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    set_content_type_from_file(req, filepath);

    char* const chunk = rest_context->scratch;

    ssize_t read_bytes;
    esp_err_t r;
    /* Read file in chunks into the scratch buffer */
    /* Send the buffer contents as HTTP response chunk */
    while(
        ((read_bytes = read(fd, chunk, SCRATCH_BUFSIZE)) > 0) &&
        ((r = httpd_resp_send_chunk(req, chunk, read_bytes)) == ESP_OK)) {

    }

    /* Close file after sending complete */
    close(fd);

    if (read_bytes < 0) {
        ESP_LOGE(TAG, "Failed to read file : %s", filepath);
    }

    if(r == ESP_OK) {
        r =  httpd_resp_send_chunk(req, NULL, 0);
    }
    return r;
}

static esp_err_t handle_options_request(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers for OPTIONS request
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // Send a blank response for OPTIONS request
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t PATCH_update_settings(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    int total_len = req->content_len;
    int cur_len = 0;
    char * buf = ((rest_server_context_t *) (req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_OK;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_OK;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON * root = cJSON_Parse(buf);
    cJSON * item;
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "stratumURL"))) {
        nvs_config_set_string(NVS_CONFIG_STRATUM_URL, item->valuestring);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "fallbackStratumURL"))) {
        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_URL, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "stratumExtranonceSubscribe")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "stratumSuggestedDifficulty")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_STRATUM_DIFFICULTY, item->valueint);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "stratumUser"))) {
        nvs_config_set_string(NVS_CONFIG_STRATUM_USER, item->valuestring);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "stratumPassword"))) {
        nvs_config_set_string(NVS_CONFIG_STRATUM_PASS, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumExtranonceSubscribe")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumSuggestedDifficulty")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY, item->valueint);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "fallbackStratumUser"))) {
        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_USER, item->valuestring);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "fallbackStratumPassword"))) {
        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_PASS, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "stratumPort")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_STRATUM_PORT, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumPort")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, item->valueint);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "ssid"))) {
        nvs_config_set_string(NVS_CONFIG_WIFI_SSID, item->valuestring);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "wifiPass"))) {
        nvs_config_set_string(NVS_CONFIG_WIFI_PASS, item->valuestring);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "hostname"))) {
        nvs_config_set_string(NVS_CONFIG_HOSTNAME, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "coreVoltage")) != NULL && item->valueint > 0) {
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "frequency")) != NULL && item->valuedouble > 0) {
        float frequency = item->valuedouble;
        nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY_FLOAT, frequency);
        // also store as u16 for backwards compatibility
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQUENCY, (int) frequency);
    }
    if ((item = cJSON_GetObjectItem(root, "overheat_mode")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
    }
    if (cJSON_IsString(item = cJSON_GetObjectItem(root, "display"))) {
        nvs_config_set_string(NVS_CONFIG_DISPLAY, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "rotation")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_ROTATION, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "invertscreen")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_INVERT_SCREEN, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "displayTimeout")) != NULL) {
        nvs_config_set_i32(NVS_CONFIG_DISPLAY_TIMEOUT, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "autofanspeed")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "fanspeed")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "minFanSpeed")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_MIN_FAN_SPEED, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "temptarget")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_TEMP_TARGET, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "statsFrequency")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_STATISTICS_FREQUENCY, item->valueint);
        statistics_set_collection_interval(item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "overclockEnabled")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_OVERCLOCK_ENABLED, item->valueint);
    }

    cJSON_Delete(root);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t POST_restart(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Restarting System because of API Request");

    // Send HTTP response before restarting
    const char* resp_str = "System will restart shortly.";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    // Delay to ensure the response is sent
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Restart the system
    esp_restart();

    __builtin_unreachable();

    // This return statement will never be reached, but it's good practice to include it
    return ESP_OK;
}

static void sendHeapInfo(http_writer_t* const w, const char* const heapName, const uint32_t caps) {

    const size_t size = heap_caps_get_total_size(caps);
    const size_t unused = heap_caps_get_free_size(caps);
    const size_t minUnused = heap_caps_get_minimum_free_size(caps);

    http_json_start_obj(w,NULL);

        http_json_write_item(w, "name", heapName);

        http_json_write_item(w, "size", size);
        http_json_write_item(w, "free", unused);
        http_json_write_item(w, "minFree", minUnused);
        http_json_write_item(w, "used", size-unused);
        http_json_write_item(w, "usedPercent", (100*(size-unused))/size);

    http_json_end_obj(w);
}

/**
 * @brief Output a string from NVS as a JSON item to the writer.
 * 
 * @param w 
 * @param name 
 * @param key 
 * @param defaultValue 
 * @return esp_err_t 
 */
static void http_json_write_nvss(http_writer_t* const w, const char* const name,
                            const char* const key, const char* const defaultValue) {
    char* const str = nvs_config_get_string(key,defaultValue);
    http_json_write_item(w,name,str);
    free(str);
}

static esp_err_t GET_system_info_dash(httpd_req_t* const req) {
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    http_writer_t wrtr;
    http_writer_t* const w = &wrtr;
    http_writer_init(w,req);

    http_json_start_obj(w,NULL);
    
    http_json_write_nvss(w, "hostname", NVS_CONFIG_HOSTNAME, CONFIG_LWIP_LOCAL_HOSTNAME);
    http_json_write_item(w, "hashRate", GLOBAL_STATE.SYSTEM_MODULE.current_hashrate);

    http_json_write_item(w, "power", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.power);
    http_json_write_item(w, "temp", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.chip_temp_avg);
    http_json_write_item(w, "temp2", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.chip_temp2_avg);
    http_json_write_item(w, "vrTemp", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.vr_temp);
    http_json_write_item(w, "maxPower", GLOBAL_STATE.DEVICE_CONFIG.family.max_power);
    http_json_write_item(w, "voltage", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.voltage);
    http_json_write_item(w, "current", Power_get_current(&GLOBAL_STATE));
    http_json_write_item(w, "nominalVoltage", GLOBAL_STATE.DEVICE_CONFIG.family.nominal_voltage);
    http_json_write_item(w, "frequency", nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY_FLOAT, CONFIG_ASIC_FREQUENCY));
    http_json_write_item(w, "coreVoltage", nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE));
    http_json_write_item(w, "coreVoltageActual", VCORE_get_voltage_mv(&GLOBAL_STATE));    

    http_json_write_item(w, "isUsingFallbackStratum", GLOBAL_STATE.SYSTEM_MODULE.is_using_fallback);

    http_json_write_nvss(w, "stratumURL", NVS_CONFIG_STRATUM_URL,CONFIG_STRATUM_URL);
    http_json_write_item(w, "stratumPort", nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT, CONFIG_STRATUM_PORT));
    http_json_write_nvss(w, "stratumUser", NVS_CONFIG_STRATUM_USER, CONFIG_STRATUM_USER);

    http_json_write_nvss(w, "fallbackStratumURL", NVS_CONFIG_FALLBACK_STRATUM_URL, CONFIG_FALLBACK_STRATUM_URL);
    http_json_write_item(w, "fallbackStratumPort", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, CONFIG_FALLBACK_STRATUM_PORT));
    http_json_write_nvss(w, "fallbackStratumUser", NVS_CONFIG_FALLBACK_STRATUM_USER, CONFIG_FALLBACK_STRATUM_USER);

    http_json_write_item(w, "bestDiff", GLOBAL_STATE.SYSTEM_MODULE.best_diff_string);

    http_json_write_item(w, "responseTime", GLOBAL_STATE.SYSTEM_MODULE.response_time);

    http_json_end_obj(w);

    return http_writer_finish(w);
}

/* Simple handler for getting system handler */
static esp_err_t GET_system_info(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // char * ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID, CONFIG_ESP_WIFI_SSID);
    // char * hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME, CONFIG_LWIP_LOCAL_HOSTNAME);
    // char * stratumURL = nvs_config_get_string(NVS_CONFIG_STRATUM_URL, CONFIG_STRATUM_URL);
    // char * fallbackStratumURL = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL, CONFIG_FALLBACK_STRATUM_URL);
    // char * stratumUser = nvs_config_get_string(NVS_CONFIG_STRATUM_USER, CONFIG_STRATUM_USER);
    // char * fallbackStratumUser = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER, CONFIG_FALLBACK_STRATUM_USER);
    // char * display = nvs_config_get_string(NVS_CONFIG_DISPLAY, "SSD1306 (128x32)");
    float frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY_FLOAT, CONFIG_ASIC_FREQUENCY);
    float expected_hashrate = frequency * GLOBAL_STATE.DEVICE_CONFIG.family.asic.small_core_count * GLOBAL_STATE.DEVICE_CONFIG.family.asic_count / 1000.0;

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char formattedMac[18];
    snprintf(formattedMac, sizeof(formattedMac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    int8_t wifi_rssi = -128;
    get_wifi_current_rssi(&wifi_rssi);

    http_writer_t wrtr;
    http_writer_t* const w = &wrtr;
    http_writer_init(w,req);

    http_json_start_obj(w,NULL);

    http_json_write_item(w, "power", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.power);
    http_json_write_item(w, "voltage", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.voltage);
    http_json_write_item(w, "current", Power_get_current(&GLOBAL_STATE));
    http_json_write_item(w, "temp", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.chip_temp_avg);
    http_json_write_item(w, "temp2", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.chip_temp2_avg);
    http_json_write_item(w, "vrTemp", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.vr_temp);
    http_json_write_item(w, "maxPower", GLOBAL_STATE.DEVICE_CONFIG.family.max_power);
    http_json_write_item(w, "nominalVoltage", GLOBAL_STATE.DEVICE_CONFIG.family.nominal_voltage);
    http_json_write_item(w, "hashRate", GLOBAL_STATE.SYSTEM_MODULE.current_hashrate);
    http_json_write_item(w, "expectedHashrate", expected_hashrate);
    http_json_write_item(w, "bestDiff", GLOBAL_STATE.SYSTEM_MODULE.best_diff_string);
    http_json_write_item(w, "bestSessionDiff", GLOBAL_STATE.SYSTEM_MODULE.best_session_diff_string);
    http_json_write_item(w, "poolDifficulty", GLOBAL_STATE.pool_difficulty);
    http_json_write_item(w, "isUsingFallbackStratum", GLOBAL_STATE.SYSTEM_MODULE.is_using_fallback);
    http_json_write_item(w, "isPSRAMAvailable", GLOBAL_STATE.psram_is_available);

    http_json_start_arr(w, "heapInfo");

    sendHeapInfo(w, "internal", MALLOC_CAP_INTERNAL);

    if(GLOBAL_STATE.psram_is_available)
    {
        sendHeapInfo(w, "psram", MALLOC_CAP_SPIRAM);
    }
   
    http_json_end_arr(w);

    http_json_write_item(w, "freeHeap", esp_get_free_heap_size());
    http_json_write_item(w, "coreVoltage", nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE));
    http_json_write_item(w, "coreVoltageActual", VCORE_get_voltage_mv(&GLOBAL_STATE));
    http_json_write_item(w, "frequency", frequency);
    http_json_write_nvss(w, "ssid", NVS_CONFIG_WIFI_SSID, CONFIG_ESP_WIFI_SSID);
    http_json_write_item(w, "macAddr", formattedMac);
    http_json_write_nvss(w, "hostname", NVS_CONFIG_HOSTNAME, CONFIG_LWIP_LOCAL_HOSTNAME);
    http_json_write_item(w, "wifiStatus", GLOBAL_STATE.SYSTEM_MODULE.wifi_status);
    http_json_write_item(w, "wifiRSSI", wifi_rssi);
    http_json_write_item(w, "apEnabled", GLOBAL_STATE.SYSTEM_MODULE.ap_enabled);
    http_json_write_item(w, "sharesAccepted", GLOBAL_STATE.SYSTEM_MODULE.shares_accepted);
    http_json_write_item(w, "sharesRejected", GLOBAL_STATE.SYSTEM_MODULE.shares_rejected);
	

    http_json_start_arr(w, "sharesRejectedReasons");

    {
        const int cnt = GLOBAL_STATE.SYSTEM_MODULE.rejected_reason_stats_count;
        if(cnt != 0) {
            for (int i = 0; i < cnt; i++) {
                http_json_start_obj(w,NULL);

                http_json_write_item(w, "message", GLOBAL_STATE.SYSTEM_MODULE.rejected_reason_stats[i].message);
                http_json_write_item(w, "count", GLOBAL_STATE.SYSTEM_MODULE.rejected_reason_stats[i].count);

                http_json_end_obj(w);
            }
        }
    }

    http_json_end_arr(w);

    http_json_write_item(w, "uptimeSeconds", (esp_timer_get_time() - GLOBAL_STATE.SYSTEM_MODULE.start_time) / 1000000);
    http_json_write_item(w, "smallCoreCount", GLOBAL_STATE.DEVICE_CONFIG.family.asic.small_core_count);
    http_json_write_item(w, "ASICModel", GLOBAL_STATE.DEVICE_CONFIG.family.asic.name);
    http_json_write_nvss(w, "stratumURL", NVS_CONFIG_STRATUM_URL, CONFIG_STRATUM_URL);
    http_json_write_item(w, "stratumPort", nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT, CONFIG_STRATUM_PORT));
    http_json_write_nvss(w, "stratumUser", NVS_CONFIG_STRATUM_USER, CONFIG_STRATUM_USER);
    http_json_write_item(w, "stratumSuggestedDifficulty", nvs_config_get_u16(NVS_CONFIG_STRATUM_DIFFICULTY, CONFIG_STRATUM_DIFFICULTY));
    http_json_write_item(w, "stratumExtranonceSubscribe", nvs_config_get_u16(NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE, STRATUM_EXTRANONCE_SUBSCRIBE));
    http_json_write_nvss(w, "fallbackStratumURL", NVS_CONFIG_FALLBACK_STRATUM_URL, CONFIG_FALLBACK_STRATUM_URL);
    http_json_write_item(w, "fallbackStratumPort", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, CONFIG_FALLBACK_STRATUM_PORT));
    http_json_write_nvss(w, "fallbackStratumUser", NVS_CONFIG_FALLBACK_STRATUM_USER, CONFIG_FALLBACK_STRATUM_USER);
    http_json_write_item(w, "fallbackStratumSuggestedDifficulty", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY, CONFIG_FALLBACK_STRATUM_DIFFICULTY));
    http_json_write_item(w, "fallbackStratumExtranonceSubscribe", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE, FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE));
    http_json_write_item(w, "responseTime", GLOBAL_STATE.SYSTEM_MODULE.response_time);
    http_json_write_item(w, "version", esp_app_get_description()->version);
    http_json_write_item(w, "axeOSVersion", axeOSVersion);
    http_json_write_item(w, "idfVersion", esp_get_idf_version());
    http_json_write_item(w, "boardVersion", GLOBAL_STATE.DEVICE_CONFIG.board_version);
    http_json_write_item(w, "family", GLOBAL_STATE.DEVICE_CONFIG.family.name);
    http_json_write_item(w, "runningPartition", esp_ota_get_running_partition()->label);
    http_json_write_item(w, "overheat_mode", nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0));
    http_json_write_item(w, "overclockEnabled", nvs_config_get_u16(NVS_CONFIG_OVERCLOCK_ENABLED, 0));
    http_json_write_nvss(w, "display", NVS_CONFIG_DISPLAY, "SSD1306 (128x32)");
    http_json_write_item(w, "rotation", nvs_config_get_u16(NVS_CONFIG_ROTATION, 0));
    http_json_write_item(w, "invertscreen", nvs_config_get_u16(NVS_CONFIG_INVERT_SCREEN, 0));
    http_json_write_item(w, "displayTimeout", nvs_config_get_i32(NVS_CONFIG_DISPLAY_TIMEOUT, -1));
    http_json_write_item(w, "autofanspeed", nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1));
    http_json_write_item(w, "fanspeed", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.fan_perc);
    http_json_write_item(w, "minFanSpeed", nvs_config_get_u16(NVS_CONFIG_MIN_FAN_SPEED, 25));
    http_json_write_item(w, "temptarget", nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET, 60));
    http_json_write_item(w, "fanrpm", GLOBAL_STATE.POWER_MANAGEMENT_MODULE.fan_rpm);
    http_json_write_item(w, "statsFrequency", nvs_config_get_u16(NVS_CONFIG_STATISTICS_FREQUENCY, 0));
    
    http_json_end_obj(w);
    http_writer_finish(w);

    return w->result;
}

static int create_json_statistics_dashboard(cJSON * root)
{
    int prebuffer = 0;

    if (root) {
        // create array for dashboard statistics
        cJSON * statsArray = cJSON_AddArrayToObject(root, "statistics");

        struct StatisticsData statsData;
        // StatisticsNodePtr node = NULL;// *GLOBAL_STATE.STATISTICS_MODULE.statisticsList; // double pointer

        if(statisticDataNext(NULL,&statsData)) {
            do {
                cJSON *valueArray = cJSON_CreateArray();
                cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.hashrate_MHz * 0.001f));
                cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.chipTemperature));
                cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.power));
                cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.timestamp));

                cJSON_AddItemToArray(statsArray, valueArray);
                prebuffer++;
            } while(statisticDataNext(&statsData,&statsData));

        }
        // struct StatisticsData statsData;
        // StatisticsNodePtr node = statisticData(NULL,&statsData)
        // if (NULL != GLOBAL_STATE.STATISTICS_MODULE.statisticsList) {
        //     StatisticsNodePtr node = *GLOBAL_STATE.STATISTICS_MODULE.statisticsList; // double pointer


        //     while (NULL != node) {
        //         node = statisticData(node, &statsData);

        //         cJSON *valueArray = cJSON_CreateArray();
        //         cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.hashrate));
        //         cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.chipTemperature));
        //         cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.power));
        //         cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.timestamp));

        //         cJSON_AddItemToArray(statsArray, valueArray);
        //         prebuffer++;
        //     }
        // }

    }

    return prebuffer;
}

static const char LABELS[] = ",\"labels\":[\"hashRate\",\"temp\",\"vrTemp\",\"power\",\"voltage\",\"current\",\"coreVoltageActual\",\"fanspeed\",\"fanrpm\",\"wifiRSSI\",\"freeHeap\",\"timestamp\"]";

static esp_err_t sendStats(httpd_req_t* const req) {

    http_writer_t wrtr;
    http_writer_t* const w = &wrtr;    
    http_writer_init(w,req);

    http_json_start_obj(w,NULL);

    http_json_write_item(w,"currentTimestamp", (esp_timer_get_time() / (1000*100)));

    http_writer_write_data(w,LABELS,sizeof(LABELS)-1);

    http_json_start_arr(w,"statistics");

    {
        struct StatisticsData statsData;
        bool more = statisticDataNext(NULL,&statsData);
        while(more) {
            more = (http_json_write_stats(w,&statsData) == ESP_OK);
            more = more && statisticDataNext(&statsData,&statsData); 
        }
    }

    http_json_end_arr(w);
    http_json_end_obj(w);

    http_writer_finish(w);
    
    return w->result;

}

static esp_err_t GET_system_statistics(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    return sendStats(req);
}

static esp_err_t GET_system_statistics_dashboard(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    cJSON * root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "currentTimestamp", (esp_timer_get_time() / 1000));
    int prebuffer = 1;

    prebuffer += create_json_statistics_dashboard(root);

    const char * response = cJSON_PrintBuffered(root, (JSON_DASHBOARD_STATS_ELEMENT_SIZE * prebuffer), 0); // unformatted
    httpd_resp_sendstr(req, response);
    free((void *)response);

    cJSON_Delete(root);

    return ESP_OK;
}

esp_err_t POST_WWW_update(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not allowed in AP mode");
        return ESP_OK;
    }

    GLOBAL_STATE.SYSTEM_MODULE.is_firmware_update = true;
    snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_filename, 20, "www.bin");
    snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_status, 20, "Starting...");

    char buf[1000];
    int remaining = req->content_len;

    const esp_partition_t * www_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    if (www_partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WWW partition not found");
        return ESP_OK;
    }

    // Don't attempt to write more than what can be stored in the partition
    if (remaining > www_partition->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File provided is too large for device");
        return ESP_OK;
    }

    // Erase the entire www partition before writing
    ESP_ERROR_CHECK(esp_partition_erase_range(www_partition, 0, www_partition->size));

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        } else if (recv_len <= 0) {
            snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_status, 20, "Protocol Error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
            return ESP_OK;
        }

        if (esp_partition_write(www_partition, www_partition->size - remaining, (const void *) buf, recv_len) != ESP_OK) {
            snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_status, 20, "Write Error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write Error");
            return ESP_OK;
        }


        uint8_t percentage = 100 - ((remaining * 100 / req->content_len));
        snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_status, 20, "Working (%d%%)", percentage);

        remaining -= recv_len;
    }

    httpd_resp_sendstr(req, "WWW update complete\n");

    readAxeOSVersion();

    snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_status, 20, "Finished...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    GLOBAL_STATE.SYSTEM_MODULE.is_firmware_update = false;

    return ESP_OK;
}

/*
 * Handle OTA file upload
 */
esp_err_t POST_OTA_update(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not allowed in AP mode");
        return ESP_OK;
    }
    
    GLOBAL_STATE.SYSTEM_MODULE.is_firmware_update = true;
    snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_filename, 20, "esp-miner.bin");
    snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_status, 20, "Starting...");

    char buf[1000];
    esp_ota_handle_t ota_handle;
    int remaining = req->content_len;

    const esp_partition_t * ota_partition = esp_ota_get_next_update_partition(NULL);
    ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

        // Timeout Error: Just retry
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;

            // Serious Error: Abort OTA
        } else if (recv_len <= 0) {
            snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_status, 20, "Protocol Error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
            return ESP_OK;
        }

        // Successful Upload: Flash firmware chunk
        if (esp_ota_write(ota_handle, (const void *) buf, recv_len) != ESP_OK) {
            esp_ota_abort(ota_handle);
            snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_status, 20, "Write Error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write Error");
            return ESP_OK;
        }

        uint8_t percentage = 100 - ((remaining * 100 / req->content_len));

        snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_status, 20, "Working (%d%%)", percentage);

        remaining -= recv_len;
    }

    // Validate and switch to new OTA image and reboot
    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_status, 20, "Validation Error");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation / Activation Error");
        return ESP_OK;
    }

    snprintf(GLOBAL_STATE.SYSTEM_MODULE.firmware_update_status, 20, "Rebooting...");

    httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");
    ESP_LOGI(TAG, "Restarting System because of Firmware update complete");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
    __builtin_unreachable();

    return ESP_OK;
}

// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t * req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

esp_err_t start_rest_server(void)
{
    bool enter_recovery = false;
    if (init_fs() != ESP_OK) {
        // Unable to initialize the web app filesystem.
        // Enter recovery mode
        enter_recovery = true;
    }

    rest_server_context_t* rest_context = (rest_server_context_t*)allocPrefPSRAM(sizeof(rest_server_context_t));

    REST_CHECK(rest_context, "No memory for rest context", err);

    {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.uri_match_fn = httpd_uri_match_wildcard;
        config.stack_size = 8192;
        config.max_open_sockets = 20;
        config.max_uri_handlers = 20;
        config.close_fn = websocket_close_fn;
        config.lru_purge_enable = true;

        ESP_LOGI(TAG, "Starting HTTP Server");
        REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);
    }
    {
        const httpd_uri_t recovery_explicit_get_uri = {
            .uri = "/recovery", 
            .method = HTTP_GET, 
            .handler = rest_recovery_handler, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &recovery_explicit_get_uri);
    }
    // Register theme API endpoints
    ESP_ERROR_CHECK(register_theme_api_endpoints(server, rest_context));

    {
        /* URI handler for fetching system info values for dashboard */
        const httpd_uri_t system_info_dash_get_uri = {
            .uri = "/api/system/info/dash", 
            .method = HTTP_GET, 
            .handler = GET_system_info_dash,
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &system_info_dash_get_uri);
    }

    {
        /* URI handler for fetching system info */
        const httpd_uri_t system_info_get_uri = {
            .uri = "/api/system/info", 
            .method = HTTP_GET, 
            .handler = GET_system_info, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &system_info_get_uri);
    }

    {
        /* URI handler for fetching system asic values */
        const httpd_uri_t system_asic_get_uri = {
            .uri = "/api/system/asic", 
            .method = HTTP_GET, 
            .handler = GET_system_asic, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &system_asic_get_uri);
    }
    {
        /* URI handler for fetching system statistic values */
        const httpd_uri_t system_statistics_get_uri = {
            .uri = "/api/system/statistics", 
            .method = HTTP_GET, 
            .handler = GET_system_statistics, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &system_statistics_get_uri);
    }
    {
        /* URI handler for fetching system statistic values for dashboard */
        const httpd_uri_t system_statistics_dashboard_get_uri = {
            .uri = "/api/system/statistics/dashboard", 
            .method = HTTP_GET, 
            .handler = GET_system_statistics_dashboard, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &system_statistics_dashboard_get_uri);
    }

    {
        /* URI handler for WiFi scan */
        const httpd_uri_t wifi_scan_get_uri = {
            .uri = "/api/system/wifi/scan",
            .method = HTTP_GET,
            .handler = GET_wifi_scan,
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &wifi_scan_get_uri);
    }

    {
        const httpd_uri_t system_restart_uri = {
            .uri = "/api/system/restart", .method = HTTP_POST, 
            .handler = POST_restart, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &system_restart_uri);
    }
    {
        const httpd_uri_t system_restart_options_uri = {
            .uri = "/api/system/restart", 
            .method = HTTP_OPTIONS, 
            .handler = handle_options_request, 
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &system_restart_options_uri);
    }
    {
        const httpd_uri_t update_system_settings_uri = {
            .uri = "/api/system", 
            .method = HTTP_PATCH, 
            .handler = PATCH_update_settings, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &update_system_settings_uri);
    }

    {
        const httpd_uri_t system_options_uri = {
            .uri = "/api/system",
            .method = HTTP_OPTIONS,
            .handler = handle_options_request,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server, &system_options_uri);
    }

    {
        const httpd_uri_t update_post_ota_firmware = {
            .uri = "/api/system/OTA", 
            .method = HTTP_POST, 
            .handler = POST_OTA_update, 
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &update_post_ota_firmware);
    }

    {
        const httpd_uri_t update_post_ota_www = {
            .uri = "/api/system/OTAWWW", 
            .method = HTTP_POST, 
            .handler = POST_WWW_update, 
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &update_post_ota_www);
    }

    {
        const httpd_uri_t ws = {
            .uri = "/api/ws", 
            .method = HTTP_GET, 
            .handler = websocket_handler, 
            .user_ctx = NULL, 
            .is_websocket = true
        };
        httpd_register_uri_handler(server, &ws);
    }

    if (enter_recovery) {
        /* Make default route serve Recovery */
        const httpd_uri_t recovery_implicit_get_uri = {
            .uri = "/*",
            .method = HTTP_GET, 
            .handler = rest_recovery_handler, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &recovery_implicit_get_uri);

    } else {
        {
            const httpd_uri_t api_common_uri = {
                .uri = "/api/*",
                .method = HTTP_ANY,
                .handler = rest_api_common_handler,
                .user_ctx = rest_context
            };
            httpd_register_uri_handler(server, &api_common_uri);
        }
        {
            /* URI handler for getting web server files */
            const httpd_uri_t common_get_uri = {
                .uri = "/*", 
                .method = HTTP_GET, 
                .handler = file_serve_handler, 
                .user_ctx = rest_context
            };
            httpd_register_uri_handler(server, &common_get_uri);
        }
    }

    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    websocket_task_start(server);

    {
        // Start the DNS server that will redirect all queries to the softAP IP
        const dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
        dns_server_start(dns_config.item,dns_config.num_of_entries);
    }

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}
