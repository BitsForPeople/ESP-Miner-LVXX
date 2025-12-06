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

#include "esp_log.h"
#include "esp_http_server.h"
#include "websocket.h"
#include "http_server.h"

#include "websocket_intf.h"

static const char * const TAG = "websocket";

static QueueHandle_t log_queue = NULL;
typedef int client_t;
static client_t clients[MAX_WEBSOCKET_CLIENTS];
// static _Atomic int active_clients = 0;
static SemaphoreHandle_t clients_mutex = NULL;

int log_to_queue(const char *format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);

    // Calculate the required buffer size +1 for \n
    int needed_size = vsnprintf(NULL, 0, format, args_copy) + 1;
    va_end(args_copy);

    // Allocate the buffer dynamically
    char *log_buffer = (char *)calloc(needed_size, sizeof(char));
    if (log_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for log buffer");
        return 0;
    }

    // Format the string into the allocated buffer
    va_copy(args_copy, args);
    vsnprintf(log_buffer, needed_size, format, args_copy);
    va_end(args_copy);

    // Ensure the log message ends with a newline
    size_t len = strlen(log_buffer);
    if (len > 0 && log_buffer[len - 1] != '\n') {
        log_buffer[len] = '\n';
        log_buffer[len + 1] = '\0';
    }

    // Print to standard output
    printf("%s", log_buffer);

    // Send to queue for WebSocket broadcasting
    if (xQueueSendToBack(log_queue, &log_buffer, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGW(TAG, "Failed to send log to queue, freeing buffer");
        free(log_buffer);
    } else {
        xEventGroupSetBits(websocket_event_handle, WS_EVENT_MSG_AVAIL);
    }

    return 0;
}

static inline void _checkAndRestoreLogging(void) {
    if (websocket_get_client_count() == 0) {
        esp_log_set_vprintf(vprintf);
    }
}

static inline bool _removeClient(client_t* const c) {
    int fd = *c;
    if(fd != -1) {
        // TODO: Close the connection here? (Need the server handle for that...)
         *c = -1;
        if(websocket_client_removed() == 0) {
            esp_log_set_vprintf(vprintf);
        }
        return true;
    } else {
        return false;
    }
}

static inline bool _removeClientByIx(int index) {
    return _removeClient(clients + index);
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

    if(c) {
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

// static EventBits_t wait_for_client_or_msg(TickType_t maxWait) {
//     return xEventGroupWaitBits(websocket_event_handle,
//         WS_EVENT_CLIENT_REMOVED | WS_EVENT_MSG_AVAIL, true, false, maxWait);
// }

static inline bool have_clients(void) {
    return websocket_get_client_count() != 0;
}

static inline char* takeNextMsg(void) {
    char* out_msg = NULL;
    xEventGroupClearBits(websocket_event_handle, WS_EVENT_MSG_AVAIL);
    while((xQueueReceive(log_queue, &out_msg, 0) == pdFALSE) && have_clients()) {
        xEventGroupWaitBits(websocket_event_handle, WS_EVENT_MSG_AVAIL | WS_EVENT_CLIENT_REMOVED, true, false, portMAX_DELAY);
    }
    return out_msg;
}

void websocket_task(void *pvParameters)
{
    ESP_LOGI(TAG, "websocket_task starting");
    httpd_handle_t https_handle = (httpd_handle_t)pvParameters;

    log_queue = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(char*));
    if (log_queue == NULL) {
        ESP_LOGE(TAG, "Error creating queue");
        vTaskDelete(NULL);
        return;
    }

    // Nope:
    // memset(clients, -1, sizeof(clients));

    for(client_t* c = clients; c < (clients + MAX_WEBSOCKET_CLIENTS); ++c) {
        *c = -1;
    }

    clients_mutex = xSemaphoreCreateMutex();
    if (clients_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create clients mutex");
    }
    // Force the mutex handle to memory.
    asm volatile ("" : : "m" (clients_mutex));

    while (true) {
        while(!have_clients()) {
            websocket_wait_for_client_added(portMAX_DELAY);
        }

        char *message;
        while ((message = takeNextMsg()) != NULL) {

            if( xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(250)) != pdFAIL ) {

                httpd_ws_frame_t ws_pkt;
                memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                ws_pkt.payload = (uint8_t *)message;
                ws_pkt.len = strlen(message);
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;

                for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
                    int client_fd = clients[i];
                    if (client_fd != -1) {
                        if (httpd_ws_send_frame_async(https_handle, client_fd, &ws_pkt) != ESP_OK) {
                            ESP_LOGI(TAG, "Failed to send WebSocket frame to fd: %d", client_fd);
                            _removeClient(&clients[i]);
                            // remove_client(client_fd);
                            // close(client_fd);
                            // clients[i] = -1;
                            // websocket_client_removed();
                        }
                    }
                }

                xSemaphoreGive(clients_mutex);
            }
            free(message);
        }
    }
}
