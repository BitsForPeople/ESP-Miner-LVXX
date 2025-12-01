#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "dns_server.h"
#include <stdbool.h>

/*
    This defines the interface between the C and C++ code. These functions are to 
    be used by the DNS server task to safely react to stop request.
*/
#ifdef __cplusplus
extern "C" {
#endif

struct dns_server_entry_list {
    int size;
    dns_entry_pair_t* entries;
};

struct dns_server_inst;
typedef struct dns_server_inst* dns_server_inst_t;

bool dns_server_start(const dns_entry_pair_t* entries, size_t num_entries);
void dns_server_stop(void);


bool dns_server_is_stop_requested(dns_server_inst_t inst);
bool dns_server_xch_sock(dns_server_inst_t inst, int oldVal, int newVal);
int dns_server_get_sock(dns_server_inst_t inst);
void dns_server_notify_stopped(dns_server_inst_t inst);
struct dns_server_entry_list dns_server_get_entry_list(dns_server_inst_t inst);

#ifdef __cplusplus
}
#endif