#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/ringbuf.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include "websocket.h"
#include "http_server.h"

#include "websocket_intf.h"

static const char * const TAG = "websocket";

_Atomic(uint32_t) websocket_client_count;

typedef int client_t;
static client_t clients[MAX_WEBSOCKET_CLIENTS];

static SemaphoreHandle_t clients_mutex = NULL;

static RingbufHandle_t ringbuf;

void websocket_task(void *pvParameters);


bool websocket_task_start(httpd_handle_t httpserver) {
    ESP_LOGI(TAG, "websocket_task starting");

    for(client_t* c = clients; c < (clients + MAX_WEBSOCKET_CLIENTS); ++c) {
        *c = -1;
    }

    ringbuf = xRingbufferCreate(WS_LOG_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if(ringbuf == NULL) {
        ESP_LOGE(TAG, "Error creating buffer.");
        goto err;
    }

    clients_mutex = xSemaphoreCreateMutex();
    if (clients_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create clients mutex");
        goto err;
    }

    if(xTaskCreate(websocket_task, "websocket_task", 4096, httpserver, 2, NULL) == pdFAIL) {
        ESP_LOGE(TAG, "Failed to create websocket task.");
        goto err;
    }

    return true;

    err:

    if(clients_mutex) {
        vSemaphoreDelete(clients_mutex);
        clients_mutex = NULL; 
    }
    if(ringbuf) {
        vRingbufferDelete(ringbuf);
        ringbuf = NULL;
    }
    return false;
}


#ifndef LIKELY
    #define LIKELY(x) (__builtin_expect(!!(x),1))
#endif
#ifndef UNLIKELY
    #define UNLIKELY(x) (__builtin_expect(!!(x),0))
#endif

static int log_to_queue(const char *format, va_list args)
{
    int len;
    {
        va_list args_copy;
        va_copy(args_copy, args);

        // Calculate the required buffer size +1 for \n
        len = vsnprintf(NULL, 0, format, args_copy);

        va_end(args_copy);
    }
    if(LIKELY( len >= 0 )) {

        // Allocate the buffer dynamically
        char *log_buffer = (char *)malloc(len+1); // for '\0' / '\n' 
        if (LIKELY(log_buffer != NULL)) {
            {
                va_list args_copy;
                // Format the string into the allocated buffer
                va_copy(args_copy, args);
                vsnprintf(log_buffer, len+1, format, args_copy);
                va_end(args_copy);
            }
            // assert log_buffer[len] == '\0'

            // Ensure the log message ends with a newline
            if(len == 0 || log_buffer[len-1] != '\n') {
                log_buffer[len] = '\n';
                len += 1;
            }

            // Print to standard output
            printf("%s", log_buffer);


            char* lb = log_buffer;

            if(UNLIKELY(len > WS_LOG_BUFFER_SIZE)) {
                lb = lb + len - WS_LOG_BUFFER_SIZE;
                len = WS_LOG_BUFFER_SIZE;
                lb[0] = ' ';
                lb[1] = '.';
                lb[2] = '.';
                lb[3] = '.';
                lb[4] = ' ';
            }

            xRingbufferSend(ringbuf,lb,len,pdMS_TO_TICKS(100));

            free(log_buffer);

        } else {
            // Nope:
            // ESP_LOGE(TAG, "Failed to allocate memory for log buffer");
            printf("Failed to allocate memory for log buffer.\n");
        }
    } // if len >= 0
    return 0;
}


static inline bool _removeClient(client_t* const c) {
    int fd = *c;
    if(fd != -1) {
        *c = -1;
        if(websocket_client_removed() == 0) {
            esp_log_set_vprintf(vprintf);
        }
        return true;
    } else {
        return false;
    }
}

static inline client_t* _findClientSlot(const int fd) {
    client_t* ptr = clients;
    client_t* const end = clients + MAX_WEBSOCKET_CLIENTS;
    while ((*ptr != fd) && ptr < end) {
        ++ptr;
    }
    if(ptr < end) {
        return ptr;
    } else {
        return NULL;
    }    
}

static inline client_t* _findAvailableClientSlot(void) {
    return _findClientSlot(-1); // Is this cheating?
}



static esp_err_t add_client(int fd)
{
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(500)) == pdFALSE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for adding client");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    client_t* const c = _findAvailableClientSlot();
    if(c) {
        *c = fd;
        ret = ESP_OK;
        if(websocket_client_added() == 0) {
            // We just added the first WS client.
            esp_log_set_vprintf(log_to_queue);
        }
    } 

    xSemaphoreGive(clients_mutex);

    if(ret == ESP_OK) {
        ESP_LOGI(TAG, "Added WebSocket client, fd: %d", fd);
    } else {
        ESP_LOGW(TAG, "Max WebSocket clients reached, cannot add fd: %d", fd);
    }


    return ret;
}


static void remove_client(int fd)
{
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for removing client");
        return;
    }

    int* const c = _findClientSlot(fd);
    if(c) {
        *c = -1;
        if(websocket_client_removed() == 0) {
            // We just removed the last WS client.
            esp_log_set_vprintf(vprintf);
        }
    }

    xSemaphoreGive(clients_mutex);

    if(c) {
        ESP_LOGI(TAG, "Removed WebSocket client, fd: %d", fd);
    }

}

void websocket_close_fn(httpd_handle_t hd, int fd)
{
    ESP_LOGI(TAG, "WebSocket client disconnected, fd: %d", fd);
    remove_client(fd);
    close(fd);
}

esp_err_t websocket_handler(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (req->method == HTTP_GET) {
        if (websocket_get_client_count() >= MAX_WEBSOCKET_CLIENTS) {
            ESP_LOGE(TAG, "Max WebSocket clients reached, rejecting new connection");
            esp_err_t ret = httpd_resp_send_custom_err(req, "429 Too Many Requests", "Max WebSocket clients reached");
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send error response: %s", esp_err_to_name(ret));
            }
            int fd = httpd_req_to_sockfd(req);
            if (fd >= 0) {
                ESP_LOGI(TAG, "Closing fd: %d for rejected connection", fd);
                httpd_sess_trigger_close(req->handle, fd);
            }
            return ret;
        }

        int fd = httpd_req_to_sockfd(req);
        esp_err_t ret = add_client(fd);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Unexpected failure adding client, fd: %d", fd);
            ret = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unexpected failure adding client");
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send error response: %s", esp_err_to_name(ret));
            }
            ESP_LOGI(TAG, "Closing fd: %d for failed client addition", fd);
            httpd_sess_trigger_close(req->handle, fd);
            return ret;
        }
        ESP_LOGI(TAG, "WebSocket handshake successful, fd: %d", fd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK || ws_pkt.len == 0) {
        ESP_LOGE(TAG, "Failed to get WebSocket frame size: %s", esp_err_to_name(ret));
        remove_client(httpd_req_to_sockfd(req));
        return ret;
    }

    uint8_t *buf = (uint8_t *)calloc(ws_pkt.len, sizeof(uint8_t));
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for WebSocket frame buffer");
        remove_client(httpd_req_to_sockfd(req));
        return ESP_FAIL;
    }

    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket frame receive failed: %s", esp_err_to_name(ret));
        free(buf);
        remove_client(httpd_req_to_sockfd(req));
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WebSocket close frame received, fd: %d", httpd_req_to_sockfd(req));
        free(buf);
        remove_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    // TODO: Handle incoming packets here

    free(buf);
    return ESP_OK;
}

static inline bool have_clients(void) {
    return websocket_get_client_count() != 0;
}

void websocket_task(void *pvParameters)
{
    httpd_handle_t https_handle = (httpd_handle_t)pvParameters;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    while (true) {

        ws_pkt.payload = (uint8_t*)xRingbufferReceiveUpTo(ringbuf,&ws_pkt.len,portMAX_DELAY,WS_LOG_BUFFER_SIZE);
        if(LIKELY(ws_pkt.payload)) {
            if( have_clients() && xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdFAIL ) {

                for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
                    int client_fd = clients[i];
                    if (client_fd != -1) {
                        if (httpd_ws_send_frame_async(https_handle, client_fd, &ws_pkt) != ESP_OK) {
                            ESP_LOGI(TAG, "Failed to send WebSocket frame to fd: %d", client_fd);
                            _removeClient(&clients[i]);
                        }
                    }
                }

                xSemaphoreGive(clients_mutex);
            }
            vRingbufferReturnItem(ringbuf,ws_pkt.payload);
        }
    }
}
