#include <span>
// #include "coinbase.hpp"
// #include "coinbase.h"
#include "jobfactory.hpp"


// static_assert(sizeof())

using namespace jobfact;

static inline CB_t& of(CB_handle_t cb) {
    return *(CB_t*)cb;
}

void coinbase_init(CB_handle_t cb) {
    of(cb).reset();
}

size_t coinbase_append(CB_handle_t cb, MemSpan_t mem) {
    return of(cb).append(std::span {mem.start_u8, mem.size});
}
size_t coinbase_append_xn2(CB_handle_t cb, size_t len) {
    return of(cb).appendXn2Space(len);
}
void coinbase_set_xn2(CB_handle_t cb, uint64_t xn2) {
    of(cb).setXn2(xn2);
}
MemSpan_t coinbase_get(CB_handle_t cb) {
    return MemSpan_t { .start_u8 = of(cb).data(), .size = of(cb).size() };
}

MemSpan_t coinbase_get_space(CB_handle_t cb) {
    CB_t& b = of(cb);
    return MemSpan_t {.start_u8 = b.data() + b.size(), .size = b.MAX_SIZE - b.size()};
}