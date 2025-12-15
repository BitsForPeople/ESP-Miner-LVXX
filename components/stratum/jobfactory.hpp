#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>
#include <span>
#include <type_traits>
// #include "coinbase.hpp"
#include "utils.h"
#include "hexutils.hpp"
#include "hashpool.h"
#include "mem_cpy.h"
#include "coinbase.h"
#include "mining.h"

#include "mempool.hpp"
#include "mempool_alloc_fn.hpp"

#include "mining.h"

namespace jobfact {

    template<typename S, typename D>
    requires (!std::is_const_v<D>)
    static inline std::size_t spanCpy(const std::span<S>& src, const std::span<D>& dst) {
        std::size_t len = src.size_bytes() < dst.size_bytes() ? src.size_bytes() : dst.size_bytes();
        if(len != 0) {
            std::memcpy(dst.data(), src.data(), len);
        }
        return len;
    }

    class Nonce : public Nonce_t {
        public:
        static constexpr size_t MAX_SIZE = sizeof(Nonce_t::u8);

        static inline std::span<const uint8_t> srcSpan(const Nonce_t& n) {
            return std::span {n.u8,n.size};
        }

        static inline std::span<uint8_t> dstSpan(Nonce_t& n) {
            return std::span {n.u8, MAX_SIZE};
        }

        constexpr Nonce(void) = default;
        constexpr Nonce(const Nonce_t& other) {
            this->operator =(other);
        }
        constexpr Nonce(const char* hex) {
            this->fromHex(hex);
        }
        constexpr operator bool(void) const {
            return this->size != 0;
        }

        constexpr std::span<uint8_t> span() {
            return std::span<uint8_t> {this->u8, this->size};
        }
        constexpr std::span<const uint8_t> span() const {
            return std::span<const uint8_t> {this->u8, this->size};
        }

        Nonce& fromHex(const char* const hex) {
            size_t len = hex::hex2bin(hex,this->u8,MAX_SIZE);
            this->size = len;
            return *this;
        }

        Nonce& fromHex(const std::string_view& hex) {
            size_t len = hex::hex2bin(hex,this->u8,MAX_SIZE);
            this->size = len;
            return *this;
        }

        Nonce& operator =(const std::string_view& hex) {
            this->fromHex(hex);
            return *this;
        }

        size_t toHex(char* const out_hex) {
            return nonce_to_hex(this,out_hex);
        }

        uint8_t* cpyTo(uint8_t* dst) const {
            memcpy(dst,this->u8,this->size);
            return dst + this->size;            
        }

        template<typename D>
        std::size_t cpyTo(const std::span<D>& dst) const {
            return spanCpy(srcSpan(*this), dst);
        }

        constexpr Nonce& operator =(const Nonce_t& other) {
            if(this != &other) {
                this->size = spanCpy(srcSpan(other),dstSpan(*this));
            }
            return *this;
        }

        constexpr void cpyTo(Nonce_t& other) const {
            if(this != &other) {
                other.size = this->cpyTo(dstSpan(other));
            }
            // return *this; 
        }

        Nonce& reset() {
            this->size = 0;
            return *this;
        }

        private:
    };

    class JobId : public JobId_t {
        public:
        static constexpr size_t MAX_LEN = sizeof(JobId_t::idstr)-1;

        constexpr operator bool(void) const {
            return this->len != 0;
        }

        constexpr bool operator ==(const JobId& other) const {
            return (this == &other) ||
                    ((this->len == other.len) &&
                    memcmp(this->idstr, other.idstr, this->len) == 0);
        }

        constexpr JobId& operator =(const std::string_view& str) {
            this->from(str);
            return *this;
        }

        constexpr std::string_view str() const {
            return std::string_view {&this->idstr[0], this->len};
        }

        constexpr std::span<const uint8_t> span() const {
            return std::span {(const uint8_t*)(this->idstr), this->len};
        }

        constexpr void cpyTo(JobId_t& other) const {
            std::size_t sz = spanCpy(this->span(), std::span {other.idstr,MAX_LEN});
            other.len = sz;
            other.idstr[len] = '\0';
        } 

        constexpr JobId& reset() {
            this->len = 0;
            this->idstr[0] = '\0';
            return *this;
        }

        std::size_t fromStr(const char* str) {
            size_t len = strlcpy(this->idstr,str,MAX_LEN+1);
            if(len > MAX_LEN) {
                len = 0;
            }
            this->len = len;
            return len;
        }

        std::size_t from(const std::string_view& str) {
            size_t len = str.size();
            if(len <= MAX_LEN) {
                memcpy(this->idstr, str.data(), len);
                this->len = len;
                this->idstr[len] = '\0';
            } else {
                this->len = 0;
            }
            return this->len;
        }
        private:
    };

    static_assert(sizeof(JobId) == sizeof(JobId_t));



    static_assert(0 < (COINBASE_MAX_SIZE) && (COINBASE_MAX_SIZE) < 65536);

    class CB : public CB_mem {
        public:
            static constexpr uint16_t MAX_SIZE = (COINBASE_MAX_SIZE);

            constexpr CB() = default;
            constexpr CB(const CB& other) {
                this->operator =(other);
            }
            constexpr CB& operator =(const CB& other) {
                if(this != &other) {
                    this->sz = other.sz;
                    this->xn2Pos = other.xn2Pos;
                    this->xn2Len = other.xn2Len;
                    if(this->sz != 0) {
                        std::memcpy(this->u8, other.u8, this->sz);
                    }
                }
                return *this;
            };

            constexpr std::size_t copyTo(uint8_t* out) const {
                if(this->sz != 0) {
                    std::memcpy(out,this->u8,this->sz);
                }
                return this->sz;
            }

            constexpr CB& reset() {
                this->sz = 0;
                this->xn2Pos = 0;
                this->xn2Len = 0;
                return *this;
            }

            constexpr CB& placeXn2(const std::size_t pos, const std::size_t len) {
                if((pos+len) < this->sz) {
                    this->xn2Pos = pos;
                    this->xn2Len = len;
                }
                return *this;
            }

            constexpr uint64_t getXn2() const {
                if(xn2Len > 0) {
                    if (std::is_constant_evaluated()) {
                        uint64_t xn {0};
                        for(unsigned i = 1; i <= xn2Len; ++i) {
                            xn = xn << 8;
                            xn = xn | u8[xn2Pos+xn2Len-i];
                        }
                        return xn;
                    } else {
                        uint64_t xn = *(const uint64_t*)(xn2ptr());
                        xn = xn & getXn2Mask();
                        return xn;
                    }
                } else {
                    return 0;
                }
            }

            void xn2To(Nonce_t& out_nonce) const {
                size_t len = sizeof(out_nonce.u8) < this->xn2Len ? sizeof(out_nonce.u8) : this->xn2Len;
                std::memcpy(out_nonce.u8,this->xn2ptr(),len);
                out_nonce.size = len;
            }

            constexpr CB& setXn2(uint64_t xn2) {
                unsigned len = xn2Len;
                if(len > 0) {
                    len -= st32(xn2ptr(), (uint32_t)xn2, len);
                    if(len != 0) {
                        st32(xn2ptr() + sizeof(uint32_t), (xn2 >> 32), len );
                    }
                }
                return *this;
            }



            constexpr std::span<uint8_t> span() {
                return std::span<uint8_t> {u8,sz};
            }

            constexpr std::span<const uint8_t> span() const {
                return std::span<const uint8_t> {u8,sz};
            }

            constexpr operator std::span<uint8_t>() {
                return span();
            }

            constexpr operator std::span<const uint8_t>() const {
                return span();
            }

            constexpr uint32_t size() const {
                return sz;
            }

            constexpr uint8_t* data() {
                return u8;
            }

            constexpr const uint8_t* data() const {
                return u8;
            }

            constexpr uint32_t setSize(const uint32_t sz) {
                if(sz <= MAX_SIZE) {
                    this->sz = sz;
                    return sz;
                } else {
                    return MAX_SIZE;
                }
            }

            constexpr bool addSize(uint32_t sz) {
                if(sz <= space()) {
                    this->sz = this->sz + sz;
                    return true;
                } else {
                    return false;
                }
            }

            constexpr uint32_t append(const std::span<const uint8_t>& data) {
                const std::size_t len = min(data.size_bytes(), MAX_SIZE - this->sz);
                if(std::is_constant_evaluated()) {
                    for(std::size_t i = 0; i < len; ++i) {
                        u8[this->sz+i] = data[i];
                    }
                } else {
                    std::memcpy(this->u8+this->sz, data.data(), len);
                }
                this->sz += len;
                return len;
            }

            constexpr uint32_t append(const std::string_view& hex) {
                uint32_t len = hex::hex2bin(hex,this->tail(), this->space());
                this->sz += len;
                return len;
            }

            constexpr uint32_t append(const Nonce& nonce) {
                uint32_t len = this->space() < nonce.size ? this->space() : nonce.size;
                memcpy(this->tail(),nonce.u8,len);
                return len;
            }

            constexpr CB& operator +=(const std::span<const uint8_t>& data) {
                this->append(data);
                return *this;
            }

            constexpr CB& operator +=(const std::string_view& hex) {
                this->append(hex);
                return *this;
            }

            constexpr CB& operator +=(const Nonce& n) {
                this->append(n.span());
                return *this;
            }


            constexpr uint32_t append(const unsigned byte) {
                if(this->sz < MAX_SIZE) {
                    this->u8[this->sz] = byte;
                    this->sz += 1;
                    return 1;
                } else {
                    return 0;
                }
            }

            constexpr uint32_t appendXn2Space(const std::size_t sz) {
                if(space() >= sz) {
                    std::memset(tail(),0,sz);
                    this->xn2Pos = this->sz;
                    this->xn2Len = sz;
                    this->sz += sz;
                    return sz;
                } else {
                    return 0;
                }
            }

            uint8_t* tail(void) {
                return this->u8 + this->sz;
            }

            std::size_t space(void) const {
                return MAX_SIZE - this->sz;
            }

        private:
            // uint16_t sz {0};

            // uint16_t xn2Pos {0};
            // uint8_t xn2Len {0};

            // uint8_t u8[N] {};

            static constexpr unsigned st32(uint8_t* p, uint32_t v, unsigned cnt) {
                if(cnt >= sizeof(uint32_t) && !std::is_constant_evaluated()) {
                    *(uint32_t*)p = v;
                    return sizeof(uint32_t);
                } else {
                    for(unsigned i = 0; i < cnt; ++i) {
                        *p = v;
                        ++p;
                        v = v >> 8;
                    }
                    return cnt;
                }
            }

            uint8_t* xn2ptr() {
                return this->u8 + this->xn2Pos;
            }

            const uint8_t* xn2ptr() const {
                return this->u8 + this->xn2Pos;
            }

            constexpr uint64_t getXn2Mask(void) const {
                return getMask(xn2Len);
            }

            static constexpr uint64_t getMask(const uint32_t n) {
                uint32_t lo = -1;
                uint32_t hi = -1;
                if(n < sizeof(uint64_t)) {
                    if(n <= sizeof(uint32_t)) {
                        hi = 0;
                        if(n < sizeof(uint32_t)) {
                            lo = ~(((uint32_t)-1) << (n*8));
                        }
                    } else {
                        hi = ~(((uint32_t)-1) << ((n-sizeof(uint32_t))*8));
                    }
                }
                return (((uint64_t)hi) << 32) | lo;
                // uint64_t m = -1;
                // if(n < sizeof(uint64_t)) {
                //     m = ~(((uint32_t)-1) << (n*8)); // m >> ((sizeof(uint32_t)-n)*8);
                // }
                // // ~(((uint32_t)-1) << (n*8));
                // return m;
            }

            static constexpr uint32_t min(const uint32_t a, const uint32_t b) {
                return (a<b) ? a : b;
            }
    };

    using CB_t = CB;

    static_assert(sizeof(CB_t) == sizeof(struct CB_mem));


    struct Work {
        uint32_t version {0};
        Hash_t prevBlockHash {};
        Hash_t merkleRoot {};
        HashLink_t* merkles {nullptr};
        uint32_t target {0};
        uint32_t ntime {0};
        bool merkleValid {false};

        JobId jobId {};
        CB coinbase {};

        constexpr Work() = default;
        constexpr Work(const Work&) = delete;
        constexpr Work(Work&& other) {
            this->version = other.version;
            this->prevBlockHash = other.prevBlockHash;

            this->merkles = other.merkles;
            other.merkles = nullptr;

            this->target = other.target;
            this->ntime = other.ntime;
            this->jobId = other.jobId;
            this->coinbase = other.coinbase;

            this->merkleValid = other.merkleValid;
            if(other.merkleValid) {
                this->merkleRoot = other.merkleRoot;
            }

        }

        Work& setMerkleBranches(HashLink_t* merkles) {
            this->releaseMerkles();
            this->merkles = merkles;
            this->merkleValid = false;
            return *this;
        }

        void copyTo(bm_job* bmjob) {
            bm_job& j = *bmjob;
            j.version = this->version;
            cpyHashTo(&this->prevBlockHash, j.prev_block_hash);

            getMerkleRoot();
            cpyHashTo(&this->merkleRoot,j.merkle_root);

            j.ntime = this->ntime;
            j.target = this->target;
            // j.jid = this->jobId;
            // this->coinbase.xn2To(j.xn2);
        }

        void to_bm_job(bm_job* const out_job) {
            bm_job& job = *out_job;
            job.version = this->version;
            cpyHashTo(&this->prevBlockHash, job.prev_block_hash);
            job.ntime = this->ntime;
            job.target = this->target;
            job.starting_nonce = 0;
            {
                getMerkleRoot();
                cpyHashTo(&this->merkleRoot,job.merkle_root);
            }
            job.num_midstates = 1; // Not really...
            this->coinbase.xn2To(job.xn2);
            this->jobId.cpyTo(job.jid);

/*
    out_job->version = params->version;
    out_job->target = params->target;
    out_job->ntime = params->ntime;
    out_job->starting_nonce = 0;
    out_job->pool_diff = difficulty;

    cpyHashTo(merkle_root,out_job->merkle_root);
*/
        }


        ~Work(void) {
            this->release();
        }

        Work& operator =(Work&& other) {
            if(this != &other) {
                this->version = other.version;
                this->prevBlockHash = other.prevBlockHash;

                this->releaseMerkles();
                this->merkles = other.merkles;
                other.merkles = nullptr;

                this->target = other.target;
                this->ntime = other.ntime;
                this->jobId = other.jobId;
                this->coinbase = other.coinbase;
                this->merkleValid = other.merkleValid;
                if(other.merkleValid) {
                    this->merkleRoot = other.merkleRoot;
                }

            }
            return *this;
        }

        constexpr Work& reset(void) {
            version = 0;
            merkles = nullptr;
            target = 0;
            ntime = 0;
            merkleValid = false;

            jobId.reset();
            coinbase.reset();

            return *this;
        }

        Work& release(void) {
            releaseMerkles();
            merkles = nullptr;
            return *this;
        }

        bool appendToCb(const char* hex) {
            this->merkleValid = false;
            std::size_t cnt = hex::hex2bin(hex, coinbase.tail(), coinbase.space());
            coinbase.addSize(cnt);
            return cnt != 0;
        }

        bool setPrevBlockHash(const char* hex) {
            return hex::hex2bin(hex,prevBlockHash.u8,sizeof(prevBlockHash.u8)) == sizeof(prevBlockHash.u8);
        }

        Work& setXn2(const uint64_t xn2) {
            this->coinbase.setXn2(xn2);
            this->merkleValid = false;
            return *this;
        }



        // bool appendToCb(const void* data, std::size_t len) {
        //     const uint8_t* p = (const uint8_t*)data;
        //     return coinbase.append(std::span {p,len}) == len;
        // }

        bool appendToCb(const Nonce_t& nonceVal) {
            this->merkleValid = false;
            return coinbase.append(std::span {nonceVal.u8, nonceVal.size});
        }

        // bool appendToCb(const uint64_t value, std::size_t len) {
        //     return (len <= sizeof(value)) && appendToCb(&value,len);
        // }

        bool appendXn2Space(const std::size_t xn2Len) {
            this->merkleValid = false;
            return coinbase.appendXn2Space(xn2Len) == xn2Len;
        }

        Hash_t getMerkleRoot(void) {
            if(!this->merkleValid) {
                this->merkleRoot = calcMerkleRoot();
                this->merkleValid = true;
            }
            return this->merkleRoot;
        }

        Hash_t getMerkleRoot(void) const {
            if(!this->merkleValid) {
                return calcMerkleRoot();
            } else {
                return this->merkleRoot;
            }
        }

        Work& resetMerkleRoot(void) {
            this->merkleValid = false;
            return *this;
        }

        void calcMerkleRoot(Hash_t* const out_hash) const {
            // Hash_t both_merkles[2];

            // static_assert(sizeof(both_merkles) == 64);

            // double_sha256_bin(coinbase.data(), coinbase.size(), &both_merkles[0]);

            // const HashLink_t* hl = merkles;
            // while(hl != NULL) {
            //     cpyToHash(hl->hash.u32,&both_merkles[1]);
            //     double_sha256_bin(both_merkles[0].u8, sizeof(both_merkles), &both_merkles[0]);
            //     hl = hl->next;
            // }

            // cpyHashTo(&both_merkles[0],out_hash);
            *out_hash = calcMerkleRoot();
        }

        Hash_t calcMerkleRoot(void) const {
            Hash_t both_merkles[2];

            static_assert(sizeof(both_merkles) == 64);

            double_sha256_bin(coinbase.data(), coinbase.size(), &both_merkles[0]);

            const HashLink_t* hl = merkles;
            while(hl != NULL) {
                cpyToHash(hl->hash.u32,&both_merkles[1]);
                double_sha256_bin(both_merkles[0].u8, sizeof(both_merkles), &both_merkles[0]);
                hl = hl->next;
            }

            // cpyHashTo(&both_merkles[0],out_hash);
            return both_merkles[0];
        }

        private:
            void releaseMerkles(void) const {
                HashLink_t* hl = merkles;
                while(hl) {
                    HashLink_t* const nxt = hl->next;
                    hashpool_put(hl);
                    hl = nxt;
                }
            }

    };

    struct HashList {
        private:
            HashLink_t* first {nullptr};
            // HashLink_t* last {nullptr};
            HashLink_t** pnext {&first};
        public:

        HashList& reset(void) {
            first = nullptr;
            pnext = &first;
            return *this;
        }
        HashList& add(HashLink_t* const hl) {
            *pnext = hl;
            pnext = &(hl->next);
            return *this;
        }
        HashLink_t* finish(void) const {
            *pnext = nullptr;
            return first;
        }
    };

    struct WorkOrder {
        std::string_view jobId {};
        std::string_view prevBlockHash {};
        std::string_view coinbase1 {};
        std::string_view coinbase2 {};
        HashLink_t* merkles {};
        std::string_view versionHex {};
        std::string_view targetHex {};
        std::string_view ntimeHex {};
    };

    struct JobFactory {
        static constexpr unsigned POOL_GROW_CNT = 2;
        using pool_t = mempool::GrowingStatsMemPool<
                            Work,
                            POOL_GROW_CNT,
                            mempool::alloc::PREFER_PSRAM
                        >;
                        
        static inline pool_t workPool {};

        Nonce extranonce1 {};
        uint8_t extranonce2Len {0};


        JobFactory& setExtranonce1(const std::string_view& hex) {
            this->extranonce1 = hex;
            return *this;
        }

        JobFactory& setExtranonce2Len(const unsigned xn2Len) {
            this->extranonce2Len = xn2Len;
            return *this;
        }

        JobFactory& setExtranonce2Len(const std::string_view xn2LenStr) {
            return this->setExtranonce2Len(str2int(xn2LenStr));
        }

        JobFactory& setup(const std::string_view& xn1Hex, const unsigned xn2Len) {
            setExtranonce1(xn1Hex);
            setExtranonce2Len(xn2Len);
            return *this;
        }

        JobFactory& setup(const std::string_view& xn1Hex, const std::string_view& xn2LenStr) {
            setExtranonce1(xn1Hex);
            setExtranonce2Len(xn2LenStr);
            return *this;
        }

        Work* getWork(const WorkOrder& order) {
            Work* const wrk = workPool.take();
            if(wrk) [[likely]] {
                wrk->reset();
                makeWork(order, *wrk);
            }
            return wrk;
        }

        void returnWork(Work* const wrk) {
            if(wrk) {
                wrk->release();
                workPool.put(wrk);
            }
        }

        private:
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
            
            void makeWork(
                const WorkOrder& order,
                Work& out_work
            ) const {
                out_work.jobId = order.jobId;

                hex::hex2bin(order.prevBlockHash, out_work.prevBlockHash.u8, sizeof(out_work.prevBlockHash.u8));

                out_work.coinbase += order.coinbase1;
                out_work.coinbase += this->extranonce1;
                out_work.coinbase.appendXn2Space(this->extranonce2Len);
                out_work.coinbase += order.coinbase2;

                out_work.merkles = order.merkles;

                out_work.version = hex::hex2u32(order.versionHex);
                out_work.target = hex::hex2u32(order.targetHex);
                out_work.ntime = hex::hex2u32(order.ntimeHex);
            }
    };


    // static_assert(sizeof(Work) == 332);
}

