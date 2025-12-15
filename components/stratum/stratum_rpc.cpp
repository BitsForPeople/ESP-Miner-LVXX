#include <array>
#include <cstdlib> // atoi
#include "stratum_rpc.hpp"
#include "json_rpc.hpp"
#include "jobfactory.hpp"
#include "hexutils.hpp"
#include "hashpool.h"
#include "esp_log.h"
#include "mem_search.h"

static constexpr const char* TAG = "stratum_rpc";



using namespace jsonrpc;
using namespace jobfact;

namespace stratum {

    StratumTxIdGen<200,299> submitIdGen {};

    static std::array<jsmn::jsmntok_t,64> tkns {};

    static Work wrk;

    // static jobfact::Nonce xn1;
    // static uint8_t xn2Len;

    static JobFactory factory {};


    static inline std::string_view getRawValue(const jsmn::Tkn& t) {
        std::string_view str = t.str();
        if(t.isStr() || t.isKey()) {
            // Include the surrounding quotes:
            str = std::string_view {str.data()-1,str.size()+2};
        }
        return str;
    }

    static inline HashLink_t* hashFrom(const jsmn::Tkn& tkn) {
        HashLink_t* const hl = hashpool_take();
        hex::hex2bin(tkn.str(),hl->hash.u8, sizeof(hl->hash.u8));
        return hl;
    }


    Work* make_work(jsmn::Tkn p, bool& out_cleanJobs) {
        WorkOrder wo {};

        // 1: Job ID
        p.next();
        wo.jobId = p.str();

        // 2: prev_block_hash
        p.next();
        wo.prevBlockHash = p.str();

        // 3: coinbase_1
        p.next();
        wo.coinbase1 = p.str();

        // 4: coinbase_2
        p.next();
        wo.coinbase2 = p.str();

        // 5: array of merkle branches
        p.next();
        {
            HashList list {};
            const unsigned end = p.token().end;
            while(p.next() && (p.token().start < end)) {
                // HashLink_t* const hl = hashpool_take();
                // hex::hex2bin(p.str(),hl->hash.u8, sizeof(hl->hash.u8));
                list.add(hashFrom(p));
                // *pnext = hl; // Link up this one.
                // pnext = &hl->next; // The next one will be linked via the current one's 'next'.
            }
            wo.merkles = list.finish();
        }

        // 6: version
        wo.versionHex = p.str();

        // 7: target/nbits
        p.next();
        wo.targetHex = p.str();

        // 8: ntime
        p.next();
        wo.ntimeHex = p.str();

        // 9: clean jobs (boolean)
        p.next();
        out_cleanJobs = p && p.isTrue();

        return factory.getWork(wo);
    }

    void return_work(Work* const w) {
        factory.returnWork(w);
    }

    void make_work(const RpcMsg& rpc, const jobfact::Nonce& xn1, std::size_t xn2Len, Work& wrk, bool& out_cleanJobs) {
        wrk.release();
        wrk.reset();
        jsmn::Tkn p = rpc.getParams(); // points to the params array token

        // 1: Job ID
        p.next();
        wrk.jobId = getRawValue(p);

        // 2: prev_block_hash
        p.next();
        hex::hex2bin(p.str(), wrk.prevBlockHash.u8, sizeof(wrk.prevBlockHash.u8));

        // 3: coinbase_1
        p.next();
        wrk.coinbase += p.str();

        wrk.coinbase += xn1.span();
        wrk.coinbase.appendXn2Space(xn2Len);

        // 4: coinbase_2
        p.next();
        wrk.coinbase += p.str();

        // 5: array of merkle branches
        p.next();
        {
            HashList list {};
            const unsigned end = p.token().end;
            while(p.next() && (p.token().start < end)) {
                // HashLink_t* const hl = hashpool_take();
                // hex::hex2bin(p.str(),hl->hash.u8, sizeof(hl->hash.u8));
                list.add(hashFrom(p));
                // *pnext = hl; // Link up this one.
                // pnext = &hl->next; // The next one will be linked via the current one's 'next'.
            }
            wrk.merkles = list.finish();
        }

        // 6: version
        wrk.version = hex::hex2u32(p.str());

        // 7: target/nbits
        p.next();
        wrk.target = hex::hex2u32(p.str());

        // 8: ntime
        p.next();
        wrk.ntime = hex::hex2u32(p.str());

        // 9: clean jobs (boolean)
        p.next();
        out_cleanJobs = p && (p == jsmn::VALUE_TRUE);
        // if(!p) {
        //     ESP_LOGW(TAG, "can't find clean_jobs parameter");
        // } else {
        //     ESP_LOGW(TAG, "clean_jobs: \"%.*s\"", p.str().size(), p.str().data());
        // }
        ESP_LOGI(TAG, "v: %08" PRIx32 ", tgt: %08" PRIx32 ", ntime: %08" PRIx32, wrk.version, wrk.target, wrk.ntime);
    }




    void rpc_check_coinbase(const MemSpan_t* cb, const char* xn2) {
        CB& w = wrk.coinbase;
        hex::hex2bin(
            xn2,
            w.u8 + w.xn2Pos,
            w.xn2Len
        );
        if(w.size() != cb->size) {
            ESP_LOGE(TAG, "coinbase size mismatch: %" PRIu32 " vs %" PRIu32, (uint32_t)cb->size, (uint32_t)w.size());
        }
        uint32_t len = w.size() < cb->size ? w.size() : cb->size;
        const std::span<const uint8_t> wspan {w.span()};
        uint32_t i;
        for(i = 0; i < len; ++i) {
            if(cb->start_u8[i] != wspan[i]) {
                ESP_LOGE(TAG, "Diff @ %" PRIu32, i);
                break;
            }
        }
        if(i >= len) {
            ESP_LOGI(TAG, "Coinbases match!");
        }
    }

    static inline void handleMiningNotify(const RpcMsg& rpc, bool& out_clean) {
        Work* const wrk = make_work(rpc.getParams(),out_clean);

        uint32_t mc = 0;
        const HashLink_t* p = wrk->merkles;
        while(p) {
            ++mc;
            p = p->next;
        }

        ESP_LOGW(TAG, "Wrk: %" PRIu32 "b cb; %" PRIu32 " merkles, clean: %" PRIu32, wrk->coinbase.size(), mc, (uint32_t)(out_clean));

        return_work(wrk);
    }

    static constexpr unsigned str2int(const std::string_view& str) {
        unsigned i = 0;
        for(char c : str) {
            if(c >= '0' && c <= '9') {
                i = (i*10) + (c-'0');
            } else {
                break;
            }
        }
        return i;
    }

    static inline void handleConfig(const jsmn::Tkn& tparam) {
        factory.setExtranonce1(tparam.str());
        factory.setExtranonce2Len(tparam.getNext().str());
        // xn1.fromHex(tparam.str());
        // xn2Len = str2int(tparam.getNext().str());
    ESP_LOGI(TAG, "xn1: %.*s, xn2Len: %" PRIu8, tparam.str().size(), tparam.str().data(), factory.extranonce2Len);
    }

    static inline jsmn::ParseResult parse(const std::string_view& json) {
        jsmn::Parser parser {tkns};
        return parser.parse(json);
    }

    void rpc_handle_msg(const char* msg) {
        static constexpr size_t MAX_MSG_LEN = 16384;
        uint32_t len = mem_findStrEnd(msg,MAX_MSG_LEN) - msg;
        if(len < MAX_MSG_LEN) {
            // jsmn::Parser parser {tkns};
            jsmn::ParseResult pr = parse(std::string_view {msg,len});
            if (pr.result > 0) {
                RpcMsg rpc = parseRpc(pr);
                jsmn::Tkn tmeth = rpc.getMethod();
                if(tmeth) {
                    const std::string_view method {tmeth.str()};
                    if(method == "mining.notify") {
                        bool clean = false;
                        handleMiningNotify(rpc,clean);
                    } else
                    if(method == "mining.set_extranonce") {
                        jsmn::Tkn tparams = rpc.getParams();
                        if(tparams) {
                            jsmn::Tkn p1 = tparams.getNext();
                            handleConfig(p1);
                        }
                    }
                } else {
                    jsmn::Tkn tresult = rpc.getResult();
                    if(tresult) {
                        if(!rpc.getError() || rpc.getError().isNull()) {
                            ESP_LOGI(TAG, "Result.");
                            const std::string_view id {rpc.getId()};
                            if(id == "2") { // STRATUM_ID_SUBSCRIBE
                                // {"result":[[["mining.notify","69acec1d"]],"e865a869",8],"id":2,"error":null}
                                jsmn::Tkn t = tresult.getChildren(); // 1: Subscriptions
                                // jsmn::Tkn txn = tsubs.getNextSibling(); // 2: xn1
                                t.nextSibling(); // 2: xn1, 3: xn2Len 
                                handleConfig(t);
                            }
                        }
                    }
                }
                // if(rpc.getMethod() && rpc.getMethod().str() == "mining.notify") {
                //     // if(xn1Hex) {
                //     //     xn1.fromHex(xn1Hex);
                //     // }
                //     bool clean = false;
                //     make_work(rpc,(*(const jobfact::Nonce*)xn1),xn2Len,wrk,clean);
                //     ESP_LOGW(TAG, "Wrk: %" PRIu32 "b cb; clean: %" PRIu32, wrk.coinbase.size(), (uint32_t)(clean));
                // }
            } else {
                ESP_LOGE(TAG, "Failed to parse json rpc: %i",(int)pr.result);
            }
        }
    }

    template<typename M>
    static inline void setId(M& msg, const RpcMsg& rpc) {
        const jsmn::Tkn tid {rpc.getId()};
        if(tid && !tid.isNull()) {
            msg.id = str2int(rpc.getId());
        } else {
            msg.id = 0;
        }
    }

    static inline void parseError(const RpcMsg& rpc, StratumMsg_t* msg) {
        // jsmn::Tkn t {rpc};
        ESP_LOGI(TAG, "Error result.");

        msg->type = STRATUM_MSG_TYPE_ERROR;
        struct StratumError& em {msg->errorMsg};
        setId(em,rpc);
        // TODO: Error message
        em.error[0] = '\0';
    }

    static inline void parseSubscriptionResult(const RpcMsg& rpc, StratumMsg_t* msg) {
        // {"result":[[["mining.notify","69b7d896"]],"3c0db369",8],"id":2,"error":null}
        // xn1 & xn2len
        ESP_LOGI(TAG, "Subscribe result.");

        jsmn::Tkn r = rpc.getResult();
        auto& m = msg->extranonceMsg;
        if(r && r.isArr()) {
            r = r.getChildren();
            r.nextSibling(); // Ignore first item
            if(r) {
                const std::string_view xn1 {r.str()}; // 1: xn1
                r.next(); // 2: xn2 len
                uint8_t xn2len = str2int(r.str());

                ((jobfact::Nonce&)(m.extranonce_1)).fromHex(xn1);

                m.extranonce_2_len = xn2len;

// factory.setup(xn1,xn2len);

                msg->type = STRATUM_MSG_TYPE_SET_EXTRANONCE;

                ESP_LOGI(TAG, "XN1: %.*s, XN2-len: %" PRIu8, xn1.size(), xn1.data(),xn2len);

            }
        }
    }

    static inline void parseConfigureResult(const RpcMsg& rpc, StratumMsg_t* msg) {
        // {"result":{"version-rolling":true,"version-rolling.mask":"1fffe000"},"id":1,"error":null}

        ESP_LOGI(TAG, "Config result.");

        jsmn::Tkn r = rpc.getResult();
        if(r && r.isObj()) {
            auto& m = msg->versionMsg;
            r = r.getChildren();
            while(r) {
                if(r.isKey() && r.str() == "version-rolling.mask" && r.next()) {
                    msg->type = STRATUM_MSG_TYPE_VERSION_MASK;
                    m.versionMask = hex::hex2u32(r.str());
                    ESP_LOGI(TAG, "Version-Mask: 0x%08" PRIx32, m.versionMask);
                    break;
                } else {
                    r.next();
                }
            }
        }
    }

    static inline void parseSubmitResult(const RpcMsg& rpc, StratumMsg_t* msg) {
        // {"reject-reason":"Duplicate","result":false,"error":null,"id":10}
        // {"result":true,"error":null,"id":207}

        ESP_LOGI(TAG, "Submit result.");

        msg->type = STRATUM_MSG_TYPE_SUBMIT_RESULT;
        auto& m = msg->submitResultMsg;
        m.id = str2int(rpc.getId());
        m.success = !rpc.getResult().isFalse();

        ESP_LOGI(TAG, "Success: %d", (int)m.success);

        m.errorMsg[0] = '\0';
        if(!m.success) {
            jsmn::Tkn t {rpc.first()}; // Object
            t.next(); // Key
            while(t && t.isKey()) {
                if(t.str() == "reject-reason") {
                    t.next(); // value
                    size_t len = (sizeof(m.errorMsg)-1) < t.str().size() ? (sizeof(m.errorMsg)-1) : t.str().size();
                    std::memcpy(m.errorMsg, t.str().data(), len);
                    m.errorMsg[len] = '\0';
                    break;
                } else {
                    t.nextSibling();
                }
            }
        }
    }

    class DiffHandler {
        public:
        static constexpr std::string_view METHOD = "mining.set_difficulty";

        static constexpr bool handles(const std::string_view& method) {
            return method == METHOD;
        }

        static bool parse(jsmn::Tkn params, StratumMsg* msg) {
            ESP_LOGI(TAG, "Set Diff.");
            if(params.next()) {
                msg->type = STRATUM_MSG_TYPE_DIFFICULTY;
                // setId(msg->difficultyMsg, id);
                msg->difficultyMsg.diff = str2int(params);
                ESP_LOGI(TAG, "Diff: %d", (int)msg->difficultyMsg.diff);
            }
            return true;
        }
    };


    class NotifyHandler {
        public:
        static constexpr std::string_view METHOD = "mining.notify";

        static constexpr bool handles(const std::string_view& method) {
            return method == METHOD;
        }

        static bool parse(const jsmn::Tkn& params, StratumMsg* msg) {
            ESP_LOGI(TAG, "Notify.");
            msg->type = STRATUM_MSG_TYPE_MINING_NOTIFY;
            auto& m {msg->miningNotifyMsg};
            Work* wrk = make_work(params, m.cleanJobs);
            // m.new_work = (work_handle_t)wrk;
            ESP_LOGI(TAG, "Wrk: cb: %" PRIu32 ", xn2: %" PRIu8 ", "
                "tgt: 0x%08" PRIx32 ", "
                "ntime: 0x%08" PRIx32
                , wrk->coinbase.size()
                , wrk->coinbase.xn2Len
                , wrk->target
                , wrk->ntime
            );
            return_work(wrk);
            m.new_work = nullptr;
            return true;
        }
    };


    template<typename H1, typename ... H>
    static inline bool parseMethod(const std::string_view& method, const std::string_view& id, const jsmn::Tkn& params, StratumMsg* msg) {
        if(H1::handles(method)) {
            if constexpr (requires {H1::parse(id,params,msg);}) {
                return H1::parse(id,params,msg);
            } else {
                return H1::parse(params,msg);
            }
        } else
        if constexpr (sizeof...(H) > 0) {
            return parseMethod<H...>(method,id,params,msg);
        } else {
            msg->type = STRATUM_MSG_TYPE_UNKNOWN;
            return false;
        }
    }

    template<typename H1, typename ... H>
    static inline bool parseMethod(const RpcMsg& rpc, StratumMsg_t* msg) {
        const std::string_view m = rpc.getMethod().str();
        return parseMethod<H1,H...>(m,rpc.getId().str(),rpc.getParams(),msg);
    }

    bool rpc_parse_msg(const char* msg, StratumMsg_t* out_msg) {
        static constexpr size_t MAX_MSG_LEN = 16384;

        out_msg->type = STRATUM_MSG_TYPE_UNKNOWN;

        uint32_t len = mem_findStrEnd(msg,MAX_MSG_LEN) - msg;
        if(len < MAX_MSG_LEN) {
            // jsmn::Parser parser {tkns};
            const jsmn::ParseResult pr {parse(std::string_view {msg,len})};
            if (pr.result > 0) {
                const RpcMsg rpc {parseRpc(pr)};
                if(rpc.isMethod()) {
                    ESP_LOGI(TAG, "Method: %.*s", rpc.getMethod().str().size(), rpc.getMethod().str().data());
                    parseMethod<DiffHandler, NotifyHandler>(rpc,out_msg);
                } else 
                if(rpc.isResult()) {
                    if(!rpc.isError() || rpc.getError().isNull()) {

                        if(rpc.getId() && !rpc.getId().isNull()) {
                            const std::string_view idstr {rpc.getId().str()};
                            if(idstr.size() == 1) {
                                if(idstr[0] == ('0'+ TX_ID_CONFIGURE)) { // Configure result
                                    parseConfigureResult(rpc,out_msg);
                                } else
                                if(idstr[0] == ('0' + TX_ID_SUBSCRIBE)) { // Subscription result
                                    parseSubscriptionResult(rpc,out_msg);
                                } else {
                                    //  Not interested.
                                }
                            } else 
                            if(idstr.size() == 3) {
                                // StratumTxIdGen generates only 3-digit ids!
                                parseSubmitResult(rpc,out_msg);
                            }
                        }

                    } else {
                        // Error!
                        parseError(rpc,out_msg);
                    }
                }
            }
        }
        return true;
    }
}


const int STRATUM_TX_ID_CONFIGURE = stratum::TX_ID_CONFIGURE;
const int STRATUM_TX_ID_SUBSCRIBE = stratum::TX_ID_SUBSCRIBE;
const int STRATUM_TX_ID_AUTHORIZE = stratum::TX_ID_AUTHORIZE;
const int STRATUM_TX_ID_SUBSCRIBE_XN = stratum::TX_ID_SUBSCRIBE_XN;

bool rpc_parse_msg(const char* msg, StratumMsg_t* out_msg) {
    return stratum::rpc_parse_msg(msg,out_msg);
}

// bool rpc_parse_msg(const char* str, size_t len) {
//     return stratum::rpc_parse_msg(str,len);
// }

void rpc_check_coinbase(const MemSpan_t* cb, const char* xn2) {
    stratum::rpc_check_coinbase(cb, xn2);
}

void rpc_handle_msg(const char* msg) {
    stratum::rpc_handle_msg(msg);
}

uint32_t rpc_get_next_submit_id(void) {
    return stratum::submitIdGen.next();
}

bool rpc_is_valid_submit_id(int id) {
    return stratum::isValidSubmitId(id);
}