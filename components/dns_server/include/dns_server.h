/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef DNS_SERVER_MAX_ITEMS
#define DNS_SERVER_MAX_ITEMS 1
#endif

#include "esp_netif.h"
#include "esp_system.h"

#define DNS_SERVER_CONFIG_SINGLE(queried_name, netif_key)                                                                          \
    {                                                                                                                              \
        .num_of_entries = 1, .item = { {.name = queried_name, .if_key = netif_key} }                                               \
    }

    /**
     * @brief Definition of one DNS entry: NAME - IP (or the netif whose IP to answer)
     *
     * @note Please use string literals (or ensure they are valid during dns_server lifetime) as names, since
     * we don't take copies of the config values `name` and `if_key`
     */
    typedef struct dns_entry_pair
    {
        const char * name;   /**<! Exact match of the name field of the DNS query to answer */
        const char * if_key; /**<! Use this network interface IP to answer, only if NULL, use the static IP below */
        esp_ip4_addr_t ip;   /**<! Constant IP address to answer this query, if "if_key==NULL" */
    } dns_entry_pair_t;

    /**
     * @brief DNS server config struct defining the rules for answering DNS (A type) queries
     *
     * @note If you want to define more rules, you can set `DNS_SERVER_MAX_ITEMS` before including this header
     * Example of using 2 entries with constant IP addresses
     * \code{.c}
     * #define DNS_SERVER_MAX_ITEMS 2
     * #include "dns_server.h"
     *
     * dns_server_config_t config = {
     *   .num_of_entries = 2,
     *   .item = { {.name = "my-esp32.com", .ip = { .addr = ESP_IP4TOADDR( 192, 168, 4, 1) } } ,
     *             {.name = "my-utils.com", .ip = { .addr = ESP_IP4TOADDR( 192, 168, 4, 100) } } } };
     * start_dns_server(&config);
     * \endcode
     */
    typedef struct dns_server_config
    {
        int num_of_entries;                          /**<! Number of rules specified in the config struct */
        dns_entry_pair_t item[DNS_SERVER_MAX_ITEMS]; /**<! Array of pairs */
    } dns_server_config_t;

    /**
     * @brief Set ups and starts a simple DNS server that will respond to all A queries (IPv4)
     * based on configured rules, pairs of name and either IPv4 address or a netif ID (to respond by it's IPv4 add)
     *
     * @param entries pointer to a list of pairs of (name, IP/netif-id)
     * @param num_entries the number of entries in the list
     * @return true if the DNS server was started
     * @return false if the DNS server was not started, either because of an error or because the DNS server is already running.
     */
    bool dns_server_start(const dns_entry_pair_t* entries, size_t num_entries);

    /**
     * @brief Stops and destroys DNS server's task and structs
     */
    void dns_server_stop(void);

#ifdef __cplusplus
}
#endif
