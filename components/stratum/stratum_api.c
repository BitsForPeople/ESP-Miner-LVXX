/******************************************************************************
 *  *
 * References:
 *  1. Stratum Protocol - [link](https://reference.cash/mining/stratum-protocol)
 *****************************************************************************/

#include "stratum_api.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "lwip/sockets.h"
#include "utils.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "mem_cpy.h"
#include "mem_search.h"
#include "strbuf.h"

#include "hashpool.h"


#ifndef LIKELY
    #define LIKELY(c) __builtin_expect(!!(c),1)
#endif

#ifndef UNLIKELY
    #define UNLIKELY(c) __builtin_expect(!!(c),0)
#endif

#define BUFFER_SIZE 1024
#define MAX_EXTRANONCE_2_LEN 32
static const char* const TAG = "stratum_api";

static const uint32_t HASHPOOL_INIT_SIZE = 24;

static const unsigned JSON_RPC_MIN_CHUNK_SIZE = 512;
static const unsigned JSON_RPC_MAX_MSG_SIZE = 16384-JSON_RPC_MIN_CHUNK_SIZE-1;

static StrBuf_t strBuf = {};

// typedef struct LineBuf {
//     StrBuf_t strBuf;
//     char* line_end;
// } LineBuf_t;

// static inline void linebuf_init(LineBuf_t* const lb) {
//     strbuf_init(&lb->strBuf);
//     lb->line_end = NULL;
// }

// static inline void linebuf_release(LineBuf_t* const lb) {
//     strbuf_release(&lb->strBuf);
//     lb->line_end = NULL;
// }

// static inline void linebuf_consume_line(LineBuf_t* const lb) {
//     if(lb->line_end != NULL) {
//         strbuf_remove(&lb->strBuf,lb->line_end - lb->strBuf.buffer + 1);
//         lb->line_end = NULL;
//     }
// }

// static inline bool linebuf_ensure_space(LineBuf_t* const lb, const unsigned space) {
//     return strbuf_ensure_space(&lb->strBuf,space);
// }

// static LineBuf_t lineBuf = {};

// static char * json_rpc_buffer = NULL;
// static uint32_t json_rpc_buffer_size = 0;
static int last_parsed_request_id = -1;

static RequestTiming request_timings[MAX_REQUEST_IDS];
static bool initialized = false;

static void init_request_timings() {
    if (!initialized) {
        for (int i = 0; i < MAX_REQUEST_IDS; i++) {
            request_timings[i].timestamp_us = 0;
            request_timings[i].tracking = false;
        }
        initialized = true;
    }
}

static RequestTiming* get_request_timing(int request_id) {
    if (request_id < 0) return NULL;
    int index = request_id % MAX_REQUEST_IDS;
    return &request_timings[index];
}

void STRATUM_V1_init(void) {
    if(hashpool_get_size() == 0) {
        hashpool_grow_by(HASHPOOL_INIT_SIZE);
    }
}

void STRATUM_V1_stamp_tx(int request_id)
{
    init_request_timings();
    if (request_id >= 1) {
        RequestTiming *timing = get_request_timing(request_id);
        if (timing) {
            timing->timestamp_us = esp_timer_get_time();
            timing->tracking = true;
        }
    }
}

double STRATUM_V1_get_response_time_ms(int request_id)
{
    init_request_timings();
    if (request_id < 0) return -1.0;
    
    RequestTiming *timing = get_request_timing(request_id);
    if (!timing || !timing->tracking) {
        return -1.0;
    }
    
    double response_time = (esp_timer_get_time() - timing->timestamp_us) / 1000.0;
    timing->tracking = false;
    return response_time;
}

static void debug_stratum_tx(const char *, const unsigned);
// static int _parse_stratum_subscribe_result_message(const char * result_json_str, char ** extranonce, int * extranonce2_len);

static int stratumSend(const int sockfd, const void* const buf, const int outLen) {
    if(outLen > 0) {
        debug_stratum_tx((const char*)buf, outLen);
        return write(sockfd,buf,outLen);
    } else {
        ESP_LOGE(TAG, "Invalid size of stratum msg to send: %d", outLen);
        return -1;
    }
}

// void STRATUM_V1_initialize_buffer()
// {
//     // json_rpc_buffer = malloc(BUFFER_SIZE);
//     // json_rpc_buffer_size = BUFFER_SIZE;
//     // if (json_rpc_buffer == NULL) {
//     //     printf("Error: Failed to allocate memory for buffer\n");
//     //     exit(1);
//     // }
//     // memset(json_rpc_buffer, 0, BUFFER_SIZE);

//     if(!strbuf_alloc(&strBuf)) {
//         ESP_LOGE(TAG, "Failed to allocate StrBuf.");
//     }

// }

void cleanup_stratum_buffer()
{
    // if(json_rpc_buffer) {
    //     free(json_rpc_buffer);
    //     json_rpc_buffer = NULL;
    // }

    strbuf_release(&strBuf);
}



static const unsigned MAX_STR_LEN = 65536;

static inline uint32_t _strlen(const char* const str) {
    const char* p = mem_findStrEnd(str, MAX_STR_LEN);
    return p-str;
}

// static inline char* findNL() {
//     const char* const p = mem_findLineEnd(json_rpc_buffer,json_rpc_buffer_size);
//     if(*p == '\n') {
//         return (char*)p;
//     } else {
//         return NULL;
//     }
// }

static inline char* findNL(const char* const str, const unsigned maxLen) {
    if(maxLen != 0) {
        char* const p = mem_find_u8(str,maxLen,'\n'); // mem_findLineEnd(str,maxLen);
        if((p-str) < maxLen) {
            return (char*)p;
        } 
    }
    return NULL;
}

static inline char* findNL_sb(const StrBuf_t* const str) {
    if(str->len != 0) {
        const char* const p = mem_findLineEnd(str->buffer,str->len);
        if(*p == '\n') {
            return (char*)p;
        }
    }
    return NULL;
}

static inline char* newStr(const char* const begin, const unsigned len) {
    char* const str = malloc(len+1);
    if(str) {
        cpy_mem(begin,str,len);
        str[len] = 0;
    }
    return str;
}

/**
 * @brief  Stores the length of the last line we returned so that we can remove this
 * much data when the next line is requested.
  */
static unsigned lineLen = 0;

void STRATUM_V1_clear_jsonrpc_buffer(void) {
    StrBuf_t* const sb = &strBuf;
    strbuf_reset(sb);
}

const char* STRATUM_V1_receive_jsonrpc_line(int sockfd)
{
    StrBuf_t* const sb = &strBuf;
    if(sb->buffer == NULL) {
        strbuf_alloc(sb);
    } else {
        // Remove the data we returned previously.
        strbuf_remove(sb,lineLen);
    }

    char* nlp;
    {
        char* pstr = sb->buffer; // Start looking for the next NL here.
        int nbytes = sb->len; // number of new/unsearched bytes
        while((nlp = findNL(pstr,nbytes)) == NULL) {
            // No NL currently in buffer. Go and read some more data.

            if(UNLIKELY( sb->len > JSON_RPC_MAX_MSG_SIZE )) {
                // 16kb and no newline? Something's wrong!
                ESP_LOGW(TAG, "JSON-RPC message exceeds size limit!");
                strbuf_release(sb);
                return NULL;
            }

            // This may re-allocate, invalidating any&all pointers into the string!
            strbuf_ensure_space(sb,JSON_RPC_MIN_CHUNK_SIZE);

            // New data gets put at the end; and that's where we'll start looking for the next NL too.
            pstr = strbuf_get_end(sb); 
            
            nbytes = recv(sockfd, pstr, strbuf_get_space(sb), 0);
            if (UNLIKELY(nbytes < 0)) {
                ESP_LOGI(TAG, "Error: recv (errno %d: %s)", errno, strerror(errno));
                strbuf_release(sb);
                return NULL;
            }

            strbuf_added(sb,nbytes);
        }
    }
    
    lineLen = nlp+1-sb->buffer; // Remember the amount of data we return now.
    *nlp = 0; // Replace NL with terminator.
    return sb->buffer;
}



// char * STRATUM_V1_receive_jsonrpc_line(int sockfd)
// {
//     if (json_rpc_buffer == NULL) {
//         STRATUM_V1_initialize_buffer();
//     }
//     // char *line, *tok = NULL;
//     char recv_buffer[BUFFER_SIZE];
//     int nbytes;
//     uint32_t buflen = 0;
//     char* nlp;

//     // if (!strstr(json_rpc_buffer, "\n")) {
//     // if(!haveNL()) {
//         while((nlp = findNL()) == NULL) {
//             // memset(recv_buffer, 0, BUFFER_SIZE);
//             nbytes = recv(sockfd, recv_buffer, BUFFER_SIZE - 1, 0);
//             if (nbytes == -1) {
//                 ESP_LOGI(TAG, "Error: recv (errno %d: %s)", errno, strerror(errno));
//                 if (json_rpc_buffer) {
//                     free(json_rpc_buffer);
//                     json_rpc_buffer=0;
//                 }
//                 return 0;
//             }

//             recv_buffer[nbytes] = 0;

//             realloc_json_buffer(nbytes);
//             strncat(json_rpc_buffer, recv_buffer, nbytes);
//         }; // while(!haveNL());
//         // } while (!strstr(json_rpc_buffer, "\n"));
//     // }
    
//     uint32_t len = nlp-json_rpc_buffer;
    
//     // buflen = _strlen(json_rpc_buffer);
//     buflen = len + _strlen(nlp);
//     // *nlp = 0;
//     // tok = strtok(json_rpc_buffer, "\n");
//     // line = strdup(tok);
//     char* line = newStr(json_rpc_buffer,len);
//     // int len = _strlen(line);
    
//     if (buflen > len + 1) {
//         cpy_mem(json_rpc_buffer + len + 1, json_rpc_buffer, buflen - len + 1);
//         // memmove(json_rpc_buffer, json_rpc_buffer + len + 1, buflen - len + 1);
//     } else {
//         json_rpc_buffer[0] = 0;
//         // strcpy(json_rpc_buffer, "");
//     }
//     return line;
// }

void STRATUM_V1_parse(StratumApiV1Message * message, const char * stratum_json)
{
    ESP_LOGI(TAG, "rx: %s", stratum_json); // debug incoming stratum messages

    cJSON * json = cJSON_Parse(stratum_json);

    cJSON * id_json = cJSON_GetObjectItem(json, "id");
    int64_t parsed_id = -1;
    if (id_json != NULL && cJSON_IsNumber(id_json)) {
        parsed_id = id_json->valueint;
    }
    last_parsed_request_id = parsed_id;
    message->message_id = parsed_id;

    cJSON * method_json = cJSON_GetObjectItem(json, "method");
    stratum_method result = STRATUM_UNKNOWN;

    //if there is a method, then use that to decide what to do
    if (method_json != NULL && cJSON_IsString(method_json)) {
        if (strcmp("mining.notify", method_json->valuestring) == 0) {
            result = MINING_NOTIFY;
        } else if (strcmp("mining.set_difficulty", method_json->valuestring) == 0) {
            result = MINING_SET_DIFFICULTY;
        } else if (strcmp("mining.set_version_mask", method_json->valuestring) == 0) {
            result = MINING_SET_VERSION_MASK;
        } else if (strcmp("mining.set_extranonce", method_json->valuestring) == 0) {
            result = MINING_SET_EXTRANONCE;
        } else if (strcmp("client.reconnect", method_json->valuestring) == 0) {
            result = CLIENT_RECONNECT;
        } else {
            ESP_LOGI(TAG, "unhandled method in stratum message: %s", stratum_json);
        }

    //if there is no method, then it is a result
    } else {
        // parse results
        cJSON * result_json = cJSON_GetObjectItem(json, "result");
        cJSON * error_json = cJSON_GetObjectItem(json, "error");
        cJSON * reject_reason_json = cJSON_GetObjectItem(json, "reject-reason");

        // if the result is null, then it's a fail
        if (result_json == NULL) {
            message->response_success = false;
            message->error_str = strdup("unknown");
            
        // if it's an error, then it's a fail
        } else if (error_json != NULL && !cJSON_IsNull(error_json)) {
            message->response_success = false;
            message->error_str = strdup("unknown");
            if (parsed_id < 5) {
                result = STRATUM_RESULT_SETUP;
            } else {
                result = STRATUM_RESULT;
            }
            if (cJSON_IsArray(error_json)) {
                int len = cJSON_GetArraySize(error_json);
                if (len >= 2) {
                    cJSON * error_msg = cJSON_GetArrayItem(error_json, 1);
                    if (cJSON_IsString(error_msg)) {
                        message->error_str = strdup(cJSON_GetStringValue(error_msg));
                    }
                }
            }

        // if the result is a boolean, then parse it
        } else if (cJSON_IsBool(result_json)) {
            if (parsed_id < 5) {
                result = STRATUM_RESULT_SETUP;
            } else {
                result = STRATUM_RESULT;
            }
            if (cJSON_IsTrue(result_json)) {
                message->response_success = true;
            } else {
                message->response_success = false;
                message->error_str = strdup("unknown");
                if (cJSON_IsString(reject_reason_json)) {
                    message->error_str = strdup(cJSON_GetStringValue(reject_reason_json));
                }                
            }
        
        //if the id is STRATUM_ID_SUBSCRIBE parse it
        } else if (parsed_id == STRATUM_ID_SUBSCRIBE) {
            result = STRATUM_RESULT_SUBSCRIBE;

            cJSON * extranonce2_len_json = cJSON_GetArrayItem(result_json, 2);
            if (extranonce2_len_json == NULL) {
                ESP_LOGE(TAG, "Unable to parse extranonce2_len: %s", result_json->valuestring);
                message->response_success = false;
                goto done;
            }
            int extranonce_2_len = extranonce2_len_json->valueint;
            if (extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
                ESP_LOGW(TAG, "Extranonce_2_len %d exceeds maximum %d, clamping to maximum", 
                         extranonce_2_len, MAX_EXTRANONCE_2_LEN);
                extranonce_2_len = MAX_EXTRANONCE_2_LEN;
            }
            message->extranonce_2_len = extranonce_2_len;

            cJSON * extranonce_json = cJSON_GetArrayItem(result_json, 1);
            if (extranonce_json == NULL) {
                ESP_LOGE(TAG, "Unable parse extranonce: %s", result_json->valuestring);
                message->response_success = false;
                goto done;
            }
            message->extranonce_str = strdup(extranonce_json->valuestring);
            message->response_success = true;
        //if the id is STRATUM_ID_CONFIGURE parse it
        } else if (parsed_id == STRATUM_ID_CONFIGURE) {
            cJSON * mask = cJSON_GetObjectItem(result_json, "version-rolling.mask");
            if (mask != NULL) {
                result = STRATUM_RESULT_VERSION_MASK;
                message->version_mask = strtoul(mask->valuestring, NULL, 16);
            } else {
                ESP_LOGI(TAG, "error setting version mask: %s", stratum_json);
            }

        } else {
            ESP_LOGI(TAG, "unhandled result in stratum message: %s", stratum_json);
        }
    }

    message->method = result;

    if (message->method == MINING_NOTIFY) {

        mining_notify * new_work = malloc(sizeof(mining_notify));
        // new_work->difficulty = difficulty;
        cJSON * params = cJSON_GetObjectItem(json, "params");
        new_work->job_id = strdup(cJSON_GetArrayItem(params, 0)->valuestring);
        new_work->prev_block_hash = strdup(cJSON_GetArrayItem(params, 1)->valuestring);
        new_work->coinbase_1 = strdup(cJSON_GetArrayItem(params, 2)->valuestring);
        new_work->coinbase_2 = strdup(cJSON_GetArrayItem(params, 3)->valuestring);

        cJSON * merkle_branch = cJSON_GetArrayItem(params, 4);
        new_work->n_merkle_branches = cJSON_GetArraySize(merkle_branch);
        if (new_work->n_merkle_branches > MAX_MERKLE_BRANCHES) {
            printf("Too many Merkle branches.\n");
            abort();
            __builtin_unreachable();
        }
        
        if(LIKELY(new_work->n_merkle_branches > 0)) {
            HashLink_t* prev = hashpool_take();
            new_work->merkle__branches = prev;
            hex2bin(cJSON_GetArrayItem(merkle_branch, 0)->valuestring, prev->hash.u8, HASH_SIZE);

            for (size_t i = 1; i < new_work->n_merkle_branches; i++) {
                HashLink_t* hl = hashpool_take();
                hex2bin(cJSON_GetArrayItem(merkle_branch, i)->valuestring, hl->hash.u8, HASH_SIZE);
                prev->next = hl;
                prev = hl;
            }

            prev->next = NULL; // end of the chain.

            if(false) {
                hashpool_log_stats();
            }
            
        }

        new_work->version = strtoul(cJSON_GetArrayItem(params, 5)->valuestring, NULL, 16);
        new_work->target = strtoul(cJSON_GetArrayItem(params, 6)->valuestring, NULL, 16);
        new_work->ntime = strtoul(cJSON_GetArrayItem(params, 7)->valuestring, NULL, 16);

        message->mining_notification = new_work;

        // params can be varible length
        int paramsLength = cJSON_GetArraySize(params);
        int value = cJSON_IsTrue(cJSON_GetArrayItem(params, paramsLength - 1));
        message->should_abandon_work = value;
    } else if (message->method == MINING_SET_DIFFICULTY) {
        cJSON * params = cJSON_GetObjectItem(json, "params");
        uint32_t difficulty = cJSON_GetArrayItem(params, 0)->valueint;
        message->new_difficulty = difficulty;
    } else if (message->method == MINING_SET_VERSION_MASK) {
        cJSON * params = cJSON_GetObjectItem(json, "params");
        uint32_t version_mask = strtoul(cJSON_GetArrayItem(params, 0)->valuestring, NULL, 16);
        message->version_mask = version_mask;
    } else if (message->method == MINING_SET_EXTRANONCE) {
        cJSON * params = cJSON_GetObjectItem(json, "params");
        char * extranonce_str = cJSON_GetArrayItem(params, 0)->valuestring;
        uint32_t extranonce_2_len = cJSON_GetArrayItem(params, 1)->valueint;
        if (extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
            ESP_LOGW(TAG, "Extranonce_2_len %" PRIu32 " exceeds maximum %d, clamping to maximum", 
                     extranonce_2_len, MAX_EXTRANONCE_2_LEN);
            extranonce_2_len = MAX_EXTRANONCE_2_LEN;
        }
        message->extranonce_str = strdup(extranonce_str);
        message->extranonce_2_len = extranonce_2_len;
    }
    done:
    cJSON_Delete(json);
}

static inline void releaseHashLinks(HashLink_t* hl) {
    while (hl != NULL) {
        HashLink_t* const n = hl->next;
        hashpool_put(hl);
        hl = n;
    }
}

void STRATUM_V1_free_mining_notify(mining_notify * params)
{
    free(params->job_id);
    free(params->prev_block_hash);
    free(params->coinbase_1);
    free(params->coinbase_2);

    releaseHashLinks(params->merkle__branches);

    free(params);
}

// static int _parse_stratum_subscribe_result_message(const char * result_json_str, char ** extranonce, int * extranonce2_len)
// {
//     cJSON * root = cJSON_Parse(result_json_str);
//     if (root == NULL) {
//         ESP_LOGE(TAG, "Unable to parse %s", result_json_str);
//         return -1;
//     }
//     cJSON * result = cJSON_GetObjectItem(root, "result");
//     if (result == NULL) {
//         ESP_LOGE(TAG, "Unable to parse subscribe result %s", result_json_str);
//         return -1;
//     }

//     cJSON * extranonce2_len_json = cJSON_GetArrayItem(result, 2);
//     if (extranonce2_len_json == NULL) {
//         ESP_LOGE(TAG, "Unable to parse extranonce2_len: %s", result->valuestring);
//         return -1;
//     }
//     *extranonce2_len = extranonce2_len_json->valueint;

//     cJSON * extranonce_json = cJSON_GetArrayItem(result, 1);
//     if (extranonce_json == NULL) {
//         ESP_LOGE(TAG, "Unable parse extranonce: %s", result->valuestring);
//         return -1;
//     }
//     *extranonce = strdup(extranonce_json->valuestring);

//     cJSON_Delete(root);

//     return 0;
// }

int STRATUM_V1_subscribe(int socket, int send_uid, const char * model)
{
    // Subscribe
    char subscribe_msg[BUFFER_SIZE];
    const esp_app_desc_t *app_desc = esp_app_get_description();
    const char *version = app_desc->version;	
    const int outLen = sprintf(subscribe_msg, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": [\"bitaxe/%s/%s\"]}\n", send_uid, model, version);

    return stratumSend(socket, subscribe_msg, outLen);

    // debug_stratum_tx(subscribe_msg);

    // return write(socket, subscribe_msg, strlen(subscribe_msg));        
}

int STRATUM_V1_suggest_difficulty(int socket, int send_uid, uint32_t difficulty)
{
    char difficulty_msg[BUFFER_SIZE];
    const int outLen = sprintf(difficulty_msg, "{\"id\": %d, \"method\": \"mining.suggest_difficulty\", \"params\": [%ld]}\n", send_uid, difficulty);

    return stratumSend(socket, difficulty_msg, outLen);

    // debug_stratum_tx(difficulty_msg);

    // return write(socket, difficulty_msg, strlen(difficulty_msg));
}

int STRATUM_V1_extranonce_subscribe(int socket, int send_uid)
{
    char extranonce_msg[BUFFER_SIZE];
    const int outLen = sprintf(extranonce_msg, "{\"id\": %d, \"method\": \"mining.extranonce.subscribe\", \"params\": []}\n", send_uid);
    return stratumSend(socket, extranonce_msg, outLen);
    // debug_stratum_tx(extranonce_msg);

    // return write(socket, extranonce_msg, strlen(extranonce_msg));
}

int STRATUM_V1_authorize(int socket, int send_uid, const char * username, const char * pass)
{
    char authorize_msg[BUFFER_SIZE];
    const int outLen = sprintf(authorize_msg, "{\"id\": %d, \"method\": \"mining.authorize\", \"params\": [\"%s\", \"%s\"]}\n", send_uid, username,
            pass);
    return stratumSend(socket, authorize_msg, outLen);
    // debug_stratum_tx(authorize_msg);

    // return write(socket, authorize_msg, strlen(authorize_msg));
}

/// @param socket Socket to write to
/// @param username The client’s user name.
/// @param jobid The job ID for the work being submitted.
/// @param ntime The hex-encoded time value use in the block header.
/// @param extranonce_2 The hex-encoded value of extra nonce 2.
/// @param nonce The hex-encoded nonce value to use in the block header.
int STRATUM_V1_submit_share(int socket, int send_uid, const char * username, const char * jobid,
                            const char * extranonce_2, const uint32_t ntime,
                            const uint32_t nonce, const uint32_t version)
{
    char submit_msg[BUFFER_SIZE];
    const int outLen = sprintf(submit_msg,
            "{\"id\":%d,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%08lx\",\"%08lx\",\"%08lx\"]}\n",
            send_uid, username, jobid, extranonce_2, ntime, nonce, version);

    return stratumSend(socket,submit_msg,outLen);
    // debug_stratum_tx(submit_msg);

    // return write(socket, submit_msg, strlen(submit_msg));
}

int STRATUM_V1_configure_version_rolling(int socket, int send_uid, uint32_t * version_mask)
{
    // char configure_msg[BUFFER_SIZE * 2];
    char configure_msg[BUFFER_SIZE];    
    const int outLen = sprintf(configure_msg,
            "{\"id\": %d, \"method\": \"mining.configure\", \"params\": [[\"version-rolling\"], {\"version-rolling.mask\": "
            "\"ffffffff\"}]}\n",
            send_uid);
    return stratumSend(socket,configure_msg,outLen);
    // debug_stratum_tx(configure_msg);

    // return write(socket, configure_msg, strlen(configure_msg));
}

static void debug_stratum_tx(const char * msg, const unsigned len)
{
    STRATUM_V1_stamp_tx(last_parsed_request_id);
    //remove the trailing newline
    // char * newline = strchr(msg, '\n');
    char* newline = findNL(msg,len);
    if (newline != NULL) {
        *newline = '\0';
    }
    ESP_LOGI(TAG, "tx: %s", msg);

    //put it back!
    if (newline != NULL) {
        *newline = '\n';
    }
}
