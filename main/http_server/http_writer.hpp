#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>
// #include <charconv>
#include <cstdio>
#include <span>
#include <string_view>

#include "esp_http_server.h"
#include "http_writer.h"
#include "esp_log.h"

namespace http {

    template<typename N>
    concept floatnumber_t = std::is_floating_point_v<N>;
    template<typename N>
    concept intnumber_t = std::is_integral_v<N>;
    template<typename N>
    concept number_t = floatnumber_t<N> || intnumber_t<N>;

    struct Writer : public http_writer_t {
        static constexpr const char* TAG = "HttpWriter";

        static constexpr std::size_t BUF_SIZE = HTTP_WRITER_BUF_SIZE;

        static Writer& of(http_writer_t& w) {
            return static_cast<Writer&>(w);
        }

        constexpr Writer(httpd_req_t* const req) :
            http_writer_t {.req = req, .result = ESP_OK, .used = 0, .cont = false, .buf = {}} 
        {

        }

        constexpr bool ok(void) const {
            return this->result == ESP_OK;
        }

        constexpr explicit operator bool(void) const {
            return ok();
        }

        constexpr operator esp_err_t(void) const {
            return this->result;
        }

        
        Writer& flush(void) {
            if(ok()) {
                if(this->used != 0) {
                    if(send(this->buf,this->used)) {
                        this->used = 0;
                    }
                }
            }
            return *this;
        }

        std::size_t space(void) const {
            return BUF_SIZE - this->used;
        }

        [[gnu::noinline]]
        Writer& write(const void* data, std::size_t len) {
            if(len != 0) {
                if(len < space()) {
                    std::memcpy(head(), data, len);
                    this->used += len;
                } else {
                    if(flush()) {
                        if(len >= BUF_SIZE) {
                            send(data,len);
                        } else {
                            std::memcpy(this->buf, data, len);
                            this->used = len;
                        }
                    }
                }
            }
            return *this;
        }

        template<typename T, std::size_t X>
        Writer& write(const std::span<T,X>& data) {
            return write(data.data(),data.size_bytes());
        }

        Writer& write(const std::string_view& str) {
            return write(std::span {str.begin(),str.end()});
        }

        template<number_t N>
        [[gnu::noinline]]
        Writer& write(const N& number) {
            // constexpr bool IS_FLOAT = std::is_floating_point_v<N>;
            // if(ok()) {
            if(requireSpace(16)) {
                // std::to_chars_result tcr;
                // if constexpr (IS_FLOAT) {
                    // std::snprintf
                    // tcr = std::to_chars((char*)(head()),(char*)(bufend()),
                    // (float)number, std::chars_format::fixed);
                    int c = std::snprintf((char*)head(),space(), fmt_for<N>(), number);
                    if(c > 0) {
                        this->used += c;
                    } else {
                        this->result = ESP_FAIL;
                    }
                // } else {
                //     tcr = std::to_chars((char*)(head()),(char*)(bufend()),
                //             number);
                // }
                // if(tcr.ec == std::errc {}) {
                //     this->used = (uint8_t*)tcr.ptr - this->buf;
                //     if(this->used == BUF_SIZE) {
                //         r = flush();
                //     }
                // } else {
                //     r = ESP_FAIL;
                // }
            } 
            // }
            return *this;
        }

        [[gnu::noinline]]
        Writer& write(const char* str) {
            uint8_t* p = head();
            uint8_t* const end = bufend();
            while( p < end && *str != '\0' ) {
                *p = *str;
                ++p;
                ++str;
            }
            this->used = p - this->buf;
            if(p >= end) {
                if(flush() && *str != '\0') {
                    write(str, strlen(str));
                }
            }
            return *this;
        }

        [[gnu::noinline]]
        Writer& write(const char& ch) {
            if(requireSpace(1)) {
                this->buf[this->used] = ch;
                this->used += 1;
            }
            return *this;
        }

        Writer& finish(void) {
            if(flush()) {
                send(NULL,0);
            }
            return *this;
        }

        template<typename ... Args>
        Writer& writeValues(Args...args) {
            (write(args).ok() && ...);
            return *this;
        }


        private:

            static constexpr const char* FMT_FLOAT = "%.3f";
            static constexpr const char* FMT_INT[] = {
                "%" PRIu8,
                "%" PRId8,
                "%" PRIu16,
                "%" PRId16,
                "%" PRIu32,
                "%" PRId32,
                "%" PRIu64,
                "%" PRId64
            };


            template<number_t N>
            static constexpr const char* fmt_for(void) {
                if constexpr (std::is_integral_v<N>) {
                    unsigned i = __builtin_ctz(sizeof(N)) * 2;
                    if constexpr (std::is_signed_v<N>) {
                        i += 1;
                    }
                    return FMT_INT[i];
                } else {
                    return FMT_FLOAT;
                }
            }

            Writer& send(const void* data, const std::size_t len) {
                if(ok()) {
                    this->result = httpd_resp_send_chunk(this->req,(const char*)data,len);
                    // ESP_LOGI(TAG, "sent %" PRIu32, (uint32_t)len);
                }
                return *this;
            }

            uint8_t* bufend(void) {
                return this->buf + BUF_SIZE;
            }

            uint8_t* head(void) {
                return this->buf + this->used;
            }

            Writer& requireSpace(const std::size_t n) {
                if(n > space()) {
                    flush();
                }
                return *this;
            }

    };

}