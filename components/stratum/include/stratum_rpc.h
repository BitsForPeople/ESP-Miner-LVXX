#pragma once
#include <stdint.h>
#include "mining_types.h"

#ifdef __cplusplus
extern "C" {
#endif


#define STRATUM_MAX_ERR_LEN     (32)

struct WorkObj;
typedef struct WorkObj* work_handle_t;

extern const int STRATUM_TX_ID_CONFIGURE;
extern const int STRATUM_TX_ID_SUBSCRIBE;
extern const int STRATUM_TX_ID_AUTHORIZE;
extern const int STRATUM_TX_ID_SUBSCRIBE_XN;


typedef enum StratumMsgType : uint8_t
{
    STRATUM_MSG_TYPE_UNKNOWN,
    STRATUM_MSG_TYPE_MINING_NOTIFY,
    STRATUM_MSG_TYPE_DIFFICULTY,
    STRATUM_MSG_TYPE_VERSION_MASK,
    STRATUM_MSG_TYPE_SET_EXTRANONCE,
    STRATUM_MSG_TYPE_ERROR,
    STRATUM_MSG_TYPE_SUBMIT_RESULT,
    STRATUM_MSG_TYPE_SETUP,
    // STRATUM_MSG_TYPE_RESULT_VERSION_MASK,
    STRATUM_MSG_TYPE_SUBSCRIBE,
    STRATUM_MSG_TYPE_RECONNECT
} StratumMsgType_t;

struct StratumDiff {
    // uint32_t id;
    uint32_t diff;
};
struct StratumVersion {
    // uint32_t id;
    uint32_t versionMask;
};
struct StratumExtranonce {
    // uint32_t id;
    Nonce_t extranonce_1;
    uint8_t extranonce_2_len;
};
struct StratumError {
    uint32_t id;
    char error[STRATUM_MAX_ERR_LEN+1];
};
struct StratumSubmitResult {
    uint32_t id;
    bool success;
    char errorMsg[STRATUM_MAX_ERR_LEN+1];
};
struct StratumReconnect {
    // uint32_t id;
};
struct StratumMiningNotify {
    // uint32_t id;
    work_handle_t new_work;
    bool cleanJobs;
};
typedef struct StratumMsg {
    StratumMsgType_t type;
    union {
        struct StratumDiff difficultyMsg;
        struct StratumVersion versionMsg;
        struct StratumExtranonce extranonceMsg;
        struct StratumSubmitResult submitResultMsg;
        struct StratumReconnect reconnectMsg;
        struct StratumMiningNotify miningNotifyMsg;
        struct StratumError errorMsg;
    };    
} StratumMsg_t;

// typedef struct StratumTxIdGen {
//     char str[3];
// } StratumTxIdGen_t;

// static inline void rpc_id_gen_init(StratumTxIdGen_t* gen) {
//     gen->str[0] = '1';
//     gen->str[1] = '0';
//     gen->str[2] = '\0';
// }

// static inline void rpc_id_gen_inc(StratumTxIdGen_t* gen) {
//     char c1 = gen->str[1] + 1;
//     if(c1 > '9') {
//         c1 = '0';
//         char c10 = gen->str[0] + 1;
//         if(c10 > '9') {
//             c10 = '1';
//         }
//         gen->str[0] = c10;
//     }
//     gen->str[1] = c1; 
// }

bool rpc_parse_msg(const char* msg, StratumMsg_t* out_msg);

void rpc_check_coinbase(const MemSpan_t* cb, const char* xn2);
void rpc_handle_msg(const char* msg);

uint32_t rpc_get_next_submit_id(void);
bool rpc_is_valid_submit_id(int id);
#ifdef __cplusplus
}
#endif
