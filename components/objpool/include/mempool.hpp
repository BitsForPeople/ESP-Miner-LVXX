#pragma once
#include <cstdint>
#include <atomic>
#include <type_traits>
#include <functional>
#include <span>

namespace mempool {

    namespace {

        /**
         * @brief This union give us a pointer member which matches the alignment of \c T.
         * Specifically when alignof(T) < alignof(T*) the compiler sill generates valid
         * accesses to \c next because the packed pointer 'inherits' the alignment of \c U<T> 
         * which 'inherits' its alignment from \c T .
         * 
         * @tparam T 
         */
        template<typename T>
        union U {
            T item;
            struct __attribute__((packed)) {
                U<T>* next;
            };
        };
    }

    template<typename T>
    requires (!std::is_const_v<T> && sizeof(T) >= sizeof(U<T>))
    class MemPoolBase {

        using item_t = U<T>;

        public:
            MemPoolBase() = default;
            MemPoolBase(const MemPoolBase&) = delete;
            MemPoolBase(MemPoolBase&&) = default;

            T* take() {
                item_t* ip = items.load(std::memory_order::acquire);
                while( ip != nullptr && !items.compare_exchange_strong(ip,ip->next)) {

                }
                if(ip != nullptr) [[likely]] {
                    ip->next = nullptr;
                    return &(ip->item);
                } else {
                    return nullptr;
                }

            }

            void put(T* const obj) {
                if(obj) [[likely]] {
                    item_t* const ip = (item_t*)obj;
                    item_t* i = items.load(std::memory_order::relaxed);
                    do {
                        ip->next = i;
                    } while (!items.compare_exchange_strong(i,ip));

                }
            }

            template<std::size_t X>
            void addMem(const std::span<T,X>& mem) {
                if(!mem.empty()) {
                    item_t* const first = (item_t*)(&mem[0]);
                    item_t* last = first;

                    // 1) Link up the chain of all new objects:
                    for(T& t : mem.subspan(1)) {
                        item_t* next = (item_t*)(&t);
                        last->next = next;
                        last = next;
                    }

                    // 2) Insert the whole chain like a single item:
                    item_t* i = items.load(std::memory_order::relaxed);
                    do {
                        last->next = i;
                    } while (!items.compare_exchange_strong(i, first));
                }
            }

            void reset(void) {
                items.store(nullptr);
            }

        private:
            std::atomic<item_t*> items {nullptr};

    };

    namespace alloc {

        template<auto ALLOC_FN>
        requires requires (const size_t align, const size_t sz) {{std::invoke(ALLOC_FN,align,sz)} -> std::convertible_to<void*>;}
        class AllocatorBase {
            public:
            static void* allocate(const size_t align, const size_t sz) {
                return std::invoke(ALLOC_FN,align,sz);
            }
        };

        template<typename T, auto ALLOC_FN>
        class Allocator {
            public:
            static std::span<T> allocate(const size_t cnt) {
                if(cnt != 0) [[likely]] {
                    T* const mem = static_cast<T*>(AllocatorBase<ALLOC_FN>::allocate(alignof(T),cnt * sizeof(T)));
                    if(mem) [[likely]] {
                        return std::span<T> {mem,cnt};
                    }
                }
                return std::span<T> {};
            }
        };
    }

    template<typename T, size_t GROWCNT, auto ALLOC_FN> 
    class GrowingMemPool {
        using alloctr = alloc::Allocator<T,ALLOC_FN>;
        public:

            T* take(void) {
                T* obj = pool.take();
                if(obj == nullptr) {
                    obj = grow();
                }
                return obj;
            }

            void put(T* obj) {
                pool.put(obj);
            }

            bool growBy(const size_t cnt) {
                if(cnt == 0) {
                    return true;
                }

                const std::span<T> mem = doAlloc(cnt);
                if(!mem.empty()) {
                    pool.addMem(mem);
                    return true;
                } else {
                    return false;
                }
            }

            uint32_t getSize() const {
                return allocCnt.load(std::memory_order::relaxed);
            }

        private:
            MemPoolBase<T> pool {};
            std::atomic<uint32_t> allocCnt {};

            T* grow(void) {
                if constexpr (GROWCNT != 0) {
                    const std::span<T> mem = doAlloc(GROWCNT);
                    if(!mem.empty()) {
                        if constexpr (GROWCNT > 1) {
                            // Adding all except the first item to the pool:
                            pool.addMem(mem.subspan(1));
                        }
                        // The first item is returned directly to the caller.
                        return &mem[0];
                    }
                }
                return nullptr;
            }

            std::span<T> doAlloc(const size_t cnt) {
                const std::span<T> mem = alloctr::allocate(cnt);
                if(!mem.empty()) {
                    addToAllocCnt(cnt);
                }
                return mem;
            }

            void addToAllocCnt(const int cnt) {
                allocCnt.fetch_add(cnt,std::memory_order::relaxed);
            }
    };

} // namespace mempool