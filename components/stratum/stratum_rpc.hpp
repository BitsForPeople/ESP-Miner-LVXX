#pragma once
#include <cstdint>
#include <atomic>
#include "stratum_rpc.h"
#include "json_rpc.hpp"

namespace stratum {

    constexpr int TX_ID_CONFIGURE = 1;
    constexpr int TX_ID_SUBSCRIBE = 2;
    constexpr int TX_ID_AUTHORIZE = 3;
    constexpr int TX_ID_SUBSCRIBE_XN = 4;

    template<unsigned MIN_TX_ID = 200, unsigned MAX_TX_ID = 299>
    requires (MIN_TX_ID <= MAX_TX_ID)
    class StratumTxIdGen {
        public:
            static constexpr uint32_t MIN_ID = MIN_TX_ID;
            static constexpr uint32_t MAX_ID = MAX_TX_ID;

            uint32_t next(void) {
                uint32_t nextId = nxtId.load(std::memory_order::relaxed);
                uint32_t newNextId;
                do {
                    newNextId = nextId + 1;
                    if(newNextId > MAX_ID) {
                        newNextId = newNextId - (MAX_ID-MIN_ID+1);
                    }
                } while(!nxtId.compare_exchange_strong(nextId,newNextId));
                return nextId;
            }

            void reset(void) {
                nxtId.store(MIN_ID,std::memory_order_relaxed);
            }
        private:
            std::atomic<uint32_t> nxtId {MIN_ID};
    };

    extern StratumTxIdGen<200,299> submitIdGen;

    static inline bool isValidSubmitId(int id) {
        return id >= submitIdGen.MIN_ID && id <= submitIdGen.MAX_ID;
    }

}