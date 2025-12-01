#include "dns_server.hpp"
#include "dns_server_intf.h"

using namespace dnss;

bool dns_server_start(const dns_entry_pair_t* entries, size_t num_entries) {
    DNSServerInst* const inst = Singleton<DNSServerInst>::create(entries,num_entries);
    return inst != nullptr;
}

void dns_server_stop(void) {
    Singleton<DNSServerInst>::destroy();
}


bool dns_server_is_stop_requested(dns_server_inst_t inst) {
    return ((DNSServerInst*)inst)->isStopRequested();
}
bool dns_server_xch_sock(dns_server_inst_t inst, int oldVal, int newVal) {
    return ((DNSServerInst*)inst)->xchSock(oldVal,newVal);
}
int dns_server_get_sock(dns_server_inst_t inst) {
    return ((DNSServerInst*)inst)->getSock();
}
void dns_server_notify_stopped(dns_server_inst_t inst) {
    ((DNSServerInst*)inst)->notifyStopped();
}
struct dns_server_entry_list dns_server_get_entry_list(dns_server_inst_t inst) {
    return ((DNSServerInst*)inst)->getEntryList();
}