#pragma once
#include <functional>

#define JSMN_SMALL (1)

#include "jsmn.hpp"


namespace jsonrpc {

    struct RpcMsg : public jsmn::Parsed {
        jsmn::Tix_t ixId {};
        jsmn::Tix_t ixResult {};
        jsmn::Tix_t ixError {};
        jsmn::Tix_t ixMethod {};
        jsmn::Tix_t ixParams {};

        using jsmn::Parsed::Parsed;
        constexpr RpcMsg(const jsmn::Parsed& p) : jsmn::Parsed {p} {

        }
        constexpr RpcMsg(const RpcMsg&) = default;
        constexpr RpcMsg& operator =(const RpcMsg&) = default;

        constexpr jsmn::Tkn getId() const {
            return tok(ixId);
        }

        constexpr jsmn::Tkn getResult() const {
            return tok(ixResult);
        }

        constexpr jsmn::Tkn getError() const {
            return tok(ixError);
        }

        constexpr jsmn::Tkn getMethod() const {
            return tok(ixMethod);
        }

        constexpr bool isMethod() const {
            return getMethod();
        }

        constexpr bool isResult() const {
            return getResult() || getError();
        }

        constexpr bool isError() const {
            return getError();
        }

        constexpr jsmn::Tkn getParams() const {
            return tok(ixParams);
        }

        constexpr jsmn::Tkn first() const {
            return tok(Parsed::first());
        }

        template<typename OP>
        requires requires (OP&& op, unsigned i, jsmn::Tkn t)
            {{std::invoke(op,i,t)} -> std::convertible_to<bool>;}
        constexpr bool onParams(OP&& op) const {
            jsmn::Tkn params = getParams();
            if(params.valid() && params.isArr()) {
                const unsigned end = params.token().end;
                bool r = true;
                jsmn::Tkn p = params.getNext();
                unsigned i = 0;
                while (r && p.valid() && p.token().start <= end) {
                    r = std::invoke(op,i,p);
                    if(r) {
                        i += 1;
                        p.nextSibling();
                    }
                }
                return r;
            } else {
                return false;
            }
        }

        private:
            constexpr jsmn::Tkn tok(const jsmn::Tix_t& ix) const {
                return jsmn::Tkn {*this,ix};
            }
    };

    RpcMsg parseRpc(const jsmn::ParseResult& pr) {
        if(pr.valid()) {
            RpcMsg msg {pr};

            jsmn::Tkn t = pr.first();
            if(t.isObj()) {
                t.next();
                while (t) {
                    if(t.isKey()) {
                        const std::string_view key = t.str();
                        jsmn::Tix_t val {t.tix + 1};
                        if(key == "id") {
                            msg.ixId = val;
                        } else
                        if(key == "method") {
                            msg.ixMethod = val;
                        } else 
                        if(key == "params") {
                            msg.ixParams = val;
                        } else
                        if(key == "error") {
                            msg.ixError = val;
                        } else
                        if(key == "result") {
                            msg.ixResult = val;
                        }
                    }
                    t.nextSibling();
                }
            }

            return msg;
        } else {
            return RpcMsg {};
        }

    }

    RpcMsg parseRpc(const std::string_view& JSON, const jsmn::toklist_v& tokens) {
        jsmn::Parser parser {tokens};
        jsmn::ParseResult pr = parser.parse(JSON);
        return parseRpc(pr);
    }
};