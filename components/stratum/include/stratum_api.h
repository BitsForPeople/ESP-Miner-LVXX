#ifndef STRATUM_API_H
#define STRATUM_API_H

#include "cJSON.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include "mining_types.h"

#include "stratum_rpc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MERKLE_BRANCHES 32
#define HASH_SIZE 32
// #define COINBASE_SIZE 100
// #define COINBASE2_SIZE 128
#define MAX_REQUEST_IDS 16
#define MAX_EXTRANONCE_2_LEN 32

// typedef struct StrView {
//     const char* str;
//     uint32_t len;
// } StrView_t;


typedef enum
{
    STRATUM_UNKNOWN,
    MINING_NOTIFY,
    MINING_SET_DIFFICULTY,
    MINING_SET_VERSION_MASK,
    MINING_SET_EXTRANONCE,
    STRATUM_RESULT,
    STRATUM_RESULT_SETUP,
    STRATUM_RESULT_VERSION_MASK,
    STRATUM_RESULT_SUBSCRIBE,
    CLIENT_RECONNECT
} stratum_method;


typedef struct
{
    char *job_id;
    char *prev_block_hash;
    char *coinbase_1;
    char *coinbase_2;
    HashLink_t* merkle__branches;
    uint32_t version;
    uint32_t target;
    uint32_t ntime;
} mining_notify;

typedef struct
{
    int64_t message_id;
    // Indicates the type of request the message represents.
    stratum_method method;

    union {
        // struct {
        //     // STRATUM_RESULT_SUBSCRIBE or MINING_SET_EXTRANONCE
        //     char * extranonce_str;
        //     int extranonce_2_len;
        // };
        struct {
            // MINING_NOTIFY
            int should_abandon_work;
            mining_notify *mining_notification;
        };
        struct {
            // MINING_SET_DIFFICULTY
            uint32_t new_difficulty;
        };
        struct {
            // MINING_SET_VERSION_MASK
            uint32_t version_mask;
        };
        struct {
            // STRATUM_RESULT
            bool response_success;
            char * error_str;

            // STRATUM_RESULT_SUBSCRIBE or MINING_SET_EXTRANONCE
            char * extranonce_str;
            int extranonce_2_len;
        };
    };
} StratumApiV1Message;


typedef struct {
    int64_t timestamp_us;
    bool tracking;
} RequestTiming;


// void STRATUM_V1_initialize_buffer();

void STRATUM_V1_init(void);

static inline uint32_t STRATUM_V1_next_submit_id(void) {
    return rpc_get_next_submit_id();
}

void STRATUM_V1_clear_jsonrpc_buffer(void);

const char* STRATUM_V1_receive_jsonrpc_line(int sockfd);
// void STRATUM_V1_consume_jsonrpc(const unsigned strLen);
// void STRATUM_V1_return_jsonrpc_line(const StrView_t line);

int STRATUM_V1_subscribe(int socket, int send_uid, const char * model);

void STRATUM_V1_parse(StratumApiV1Message *message, const char *stratum_json);

void STRATUM_V1_stamp_tx(int request_id);

void STRATUM_V1_free_mining_notify(mining_notify *params);

int STRATUM_V1_authorize(int socket, int send_uid, const char *username, const char *pass);

int STRATUM_V1_configure_version_rolling(int socket, int send_uid, uint32_t * version_mask);

int STRATUM_V1_suggest_difficulty(int socket, int send_uid, uint32_t difficulty);

int STRATUM_V1_extranonce_subscribe(int socket, int send_uid);

int STRATUM_V1_submit_share(int socket, int send_uid, const char *username, const char *jobid,
                            const char *extranonce_2, const uint32_t ntime, const uint32_t nonce,
                            const uint32_t version);

double STRATUM_V1_get_response_time_ms(int request_id);

#ifdef __cplusplus
}
#endif

#endif // STRATUM_API_H