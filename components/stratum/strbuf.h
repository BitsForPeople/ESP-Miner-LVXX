#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

static const unsigned STRBUF_INITIAL_SIZE = 2048;
static const unsigned STRBUF_GROW_SIZE = 1024;

typedef struct StrBuf {
    uint32_t size; // total capacity of this buffer
    uint32_t len; // length of data in the buffer _not_ including the terminating 0; a buffer of size=10 with len=9 is full!
    char* buffer;
} StrBuf_t;

void strbuf_init(StrBuf_t* const sb);

void strbuf_release(StrBuf_t* const sb);

/**
 * @brief If the StrBuf has no buffer allocated, this allocates a buffer of \c STRBUF_INITIAL_SIZE
 * and initialized the StrBuf. Else does nothing.
 * 
 * @param sb 
 * @return true 
 * @return false 
 */
bool strbuf_alloc(StrBuf_t* const sb);

/**
 * @brief Invalidates the buffer's content, setting it to 0 length, while 
 * keeping the allocated buffer.
 * 
 * @param sb 
 */
void strbuf_clear(StrBuf_t* const sb);

/**
 * @brief Invalidates the buffer, as strbuf_clear(), and releases the allocated buffer
 * if it is > \c STRBUF_INITIAL_SIZE. The buffer is kept if it is still of the initial
 * size.
 * Before the next use, strbuf_alloc(), strbuf_resize(), or strbuf_ensure_space() should
 * be called to make sure a buffer is allocated.
 * 
 * @param sb 
 */
void strbuf_reset(StrBuf_t* const sb);

/**
 * @brief 
 * 
 * @param sb 
 * @return The remaining unused space in the buffer. 
 */
uint32_t strbuf_get_space(const StrBuf_t* const sb);

bool strbuf_resize(StrBuf_t* const sb, const uint32_t newSize);

bool strbuf_ensure_space(StrBuf_t* const sb, const uint32_t space);

/**
 * @brief Returns the pointer to the terminating '\0' at the end of the buffered string.
 * The buffer must be already allocated!
 * 
 * @param sb 
 * @return 
 */
char* strbuf_get_end(const StrBuf_t* const sb);

/**
 * @brief Advances the end of the buffered string by \p len bytes.
 * To be used after more string data was copied into the buffer (at strbuf_get_end()).
 * 
 * @param sb 
 * @param len length of data added, excluding '\0' 
 */
void strbuf_added(StrBuf_t* const sb, const uint32_t len);
bool strbuf_append(StrBuf_t* const sb, const char* data, const uint32_t len);
void strbuf_remove(StrBuf_t* const sb, const uint32_t len);

bool strbuf_grow(StrBuf_t* const sb);
bool strbuf_shrink(StrBuf_t* const sb);

#ifdef __cplusplus
}
#endif