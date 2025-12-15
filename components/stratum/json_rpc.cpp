
#include <stdio.h>
#include <string_view>
#include <array>
#include <cstdint>

#include "json_rpc.h"
#include "json_rpc.hpp"
#include "esp_log.h"


static const char* const TAG = "json_rpc";

static constexpr std::size_t TKN_BUF_SZ = 64;

static std::array<jsmn::jsmntok_t,TKN_BUF_SZ> tkns;

static uint32_t tknMax;

struct Method {
    private:
        static constexpr std::string_view PREFIX {"mining."};

        static constexpr std::string_view NOTIFY {"notify"};
        static constexpr std::string_view SET_DIFF {"set_difficulty"};
        static constexpr std::string_view SET_VERSION {"set_version_mask"};
        static constexpr std::string_view SET_EXTRANONCE {"set_extranonce"};

        static constexpr std::string_view RECONNECT {"client.reconnect"};
    public:

    enum class Id {
        NOTIFY,
        SET_DIFF,
        SET_VERSION,
        SET_EXTRANONCE,
        RECONNECT,

        UNKNOWN
    };

    static constexpr std::string_view str(const Id id) {
        switch(id) {
            case Id::NOTIFY : return "mining.notify";
            case Id::SET_DIFF : return "mining.set_difficulty";
            case Id::SET_VERSION : return "mining.set_version";
            case Id::SET_EXTRANONCE : return "mining.set_extranonce";
            case Id::RECONNECT : return RECONNECT;
            default:
                return "unknown";
        }
    }

    static constexpr Id from(const std::string_view& val) {
        if(val.starts_with(PREFIX)) {
            const std::string_view name = val.substr(PREFIX.size());
            if(name == NOTIFY) {
                return Id::NOTIFY;
            } else
            if(name == SET_DIFF) {
                return Id::SET_DIFF;
            } else
            if(name == SET_VERSION) {
                return Id::SET_VERSION;
            } else 
            if(name == SET_EXTRANONCE) {
                return Id::SET_EXTRANONCE;
            } else {
                return Id::UNKNOWN;
            }
        } else 
        if(val == RECONNECT) {
            return Id::RECONNECT;
        } else {
            return Id::UNKNOWN;
        }
    }
};

static inline std::string_view getstr(const jsmn::Tkn& t) {
    if(t) {
        return t.str();
    } else {
        return "<none>";
    }
}

static inline std::string_view getRawValue(const jsmn::Tkn& t) {
    std::string_view str = t.str();
    if(t.isStr() || t.isKey()) {
        // Include the surrounding quotes:
        str = std::string_view {str.data()-1,str.size()+2};
    }
    return str;
}

static inline void logmsg(const jsonrpc::RpcMsg& msg) {
    jsmn::Tkn t = msg.getId();
    std::string_view id = getstr(msg.getId());
    t = msg.getMethod();
    if(t) {
        const std::string_view mname = t.str();
        t = msg.getParams();
        uint32_t paramcnt = 0;
        if(t) {
            paramcnt = t.countChildren();
        }
        ESP_LOGI(TAG, "id: %.*s, method: %.*s (%" PRIu32 ")", id.size(), id.data(), mname.size(), mname.data(), paramcnt);
    } else {
        t = msg.getResult();
        if(t) {
            const std::string_view r = getstr(t);
            ESP_LOGI(TAG, "id: %.*s, result: %.*s", id.size(), id.data(), r.size(), r.data());
        } else {
            ESP_LOGI(TAG, "Unknown msg.");
        }
    }

}
void json_rpc_parse(const char* str, size_t len) {
    jsmn::Parser parser {tkns};
    jsmn::ParseResult pr = parser.parse(std::string_view {str,len});
    if(!pr.valid()) {
        ESP_LOGW(TAG, "Parse result: %i", (int)pr.result);
    } else {
        if(pr.size() > tknMax) {
            tknMax = pr.size();
        }
        ESP_LOGW(TAG, "%i tokens (max: %" PRIu32 ")", (int)pr.size(), tknMax);

        jsonrpc::RpcMsg m = jsonrpc::parseRpc(pr);
        logmsg(m);

    }
}
