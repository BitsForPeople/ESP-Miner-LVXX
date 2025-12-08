#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WS_LOG_BUFFER_SIZE      (4096)
#define MAX_WEBSOCKET_CLIENTS   (10)

bool websocket_task_start(httpd_handle_t httpserver);

esp_err_t websocket_handler(httpd_req_t * req);
void websocket_close_fn(httpd_handle_t hd, int sockfd);

#endif /* WEBSOCKET_H_ */
