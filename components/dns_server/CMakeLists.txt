idf_component_register(
SRCS
    "dns_server.cpp"
    "dns_server_task.c"

INCLUDE_DIRS
    "include"

PRIV_REQUIRES
    "esp_netif"
    "esp_wifi"
    "freertos_cpp"
)
