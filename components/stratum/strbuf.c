#include "strbuf.h"

#include "mem_cpy.h"




void strbuf_init(StrBuf_t* const sb) {
    sb->size = 0;
    sb->len = 0;
    sb->buffer = NULL;
}

void strbuf_release(StrBuf_t* const sb) {
    if(sb->buffer) {
        free(sb->buffer);
    }
    strbuf_init(sb);
}



bool strbuf_alloc(StrBuf_t* const sb) {
    if(sb->buffer != NULL) {
        // already/still allocated.
        return true;
    } else {
        strbuf_init(sb);
        sb->buffer = (char*)malloc(STRBUF_INITIAL_SIZE);
        if(sb->buffer) {
            sb->buffer[0] = 0;
            sb->size = STRBUF_INITIAL_SIZE;
            return true;
        } else {
            return false;
        }
    }
}

void strbuf_clear(StrBuf_t* const sb) {
    sb->len = 0;
    if(sb->buffer) {
        sb->buffer[0] = 0;
    }
}

void strbuf_reset(StrBuf_t* const sb) {
    strbuf_clear(sb);
    if(sb->size > STRBUF_INITIAL_SIZE) {
        // strbuf_release(sb);
        strbuf_resize(sb,STRBUF_INITIAL_SIZE);
    }
}

uint32_t strbuf_get_space(const StrBuf_t* const sb) {
    if(sb->size == 0) {
        return 0;
    } else {
        return sb->size - (sb->len+1);
    }
}

bool strbuf_resize(StrBuf_t* const sb, const uint32_t newSize) {
    if(newSize == 0) {
        strbuf_release(sb);
    }

    bool r = newSize == sb->size; // Always true if newSize == 0!
    if(!r) {
        char* const nb = (char*)realloc(sb->buffer,newSize);
        // char* const nb = (char*)malloc(newSize);
        r = nb != NULL;
        if(r) {
            sb->buffer = nb;

            // if(sb->buffer) {
                const uint32_t len = (newSize-1) < sb->len ? (newSize-1) : sb->len;
                // if(len != 0) {
                //     cpy_mem(sb->buffer,nb,len);
                // } 
                // free(sb->buffer);
                nb[len] = 0;
                // sb->buffer = nb;
                sb->len = len;
            // } else {
            //     nb[0] = 0;
            //     sb->len = 0;
            //     sb->buffer = nb;
            // }
            sb->size = newSize;
        }
    }
    return r;
}

char* strbuf_get_end(const StrBuf_t* const sb) {
    return sb->buffer + sb->len;
}

bool strbuf_ensure_space(StrBuf_t* const sb, const uint32_t space) {
    if(strbuf_get_space(sb) < space) {
        const uint32_t ns = (((sb->len + space + 1) + (STRBUF_GROW_SIZE-1)) / STRBUF_GROW_SIZE) * STRBUF_GROW_SIZE;
        return strbuf_resize(sb,ns);
    } else {
        return true;
    }
}

void strbuf_added(StrBuf_t* const sb, const uint32_t len) {
    sb->len += len;
    sb->buffer[sb->len] = 0;
}

bool strbuf_append(StrBuf_t* const sb, const char* data, const uint32_t len) {
    if(len == 0) {
        return true;
    }

    if(strbuf_ensure_space(sb,len)) {
        cpy_mem(data,strbuf_get_end(sb), len);
        sb->len += len;
        sb->buffer[sb->len] = 0;
        return true;
    } else {
        return false;
    }
}

void strbuf_remove(StrBuf_t* const sb, const uint32_t len) {
    if(len >= sb->len) {
        sb->len = 0;
        sb->buffer[0] = 0;
    } else {
        const uint32_t cp = sb->len - len;
        cpy_mem(sb->buffer + len, sb->buffer, cp);
        sb->len = cp;
        sb->buffer[sb->len] = 0;
    }
}

bool strbuf_grow(StrBuf_t* const sb) {
    return strbuf_resize(sb,sb->size + STRBUF_GROW_SIZE);
}

bool strbuf_shrink(StrBuf_t* const sb) {
    if(sb->size > STRBUF_INITIAL_SIZE) {
        return strbuf_resize(sb,sb->size-STRBUF_GROW_SIZE);
    } else {
        return true;
    }
}