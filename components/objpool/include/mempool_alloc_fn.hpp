#pragma once

namespace mempool {
    namespace alloc {

        inline void* PREFER_PSRAM(size_t align, size_t sz) {
            void* mem = heap_caps_aligned_alloc(align, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if(!mem) [[unlikely]] {
                mem = heap_caps_aligned_alloc(align, sz, MALLOC_CAP_8BIT);
            }
            return mem;
        }

        inline void* INTERNAL(size_t align, size_t sz) {
            return  heap_caps_aligned_alloc(align, sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        };

        inline void* PREFER_INTERNAL(size_t align, size_t sz) {
            void* mem = heap_caps_aligned_alloc(align, sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if(!mem) [[unlikely]] {
                mem = heap_caps_aligned_alloc(align, sz, MALLOC_CAP_8BIT);
            }
            return mem;
        };
        
    }        
}