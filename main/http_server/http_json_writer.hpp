#pragma once
#include "http_writer.hpp"
#include <string_view>

namespace http::json {

    class JsonWriter : public http::Writer {
        public:

        static constexpr JsonWriter& of(http::Writer& w) {
            return static_cast<JsonWriter&>(w);
        }

        static constexpr JsonWriter& of(http_writer_t& w) {
            return JsonWriter::of(http::Writer::of(w));
        }

        using http::Writer::Writer;

        JsonWriter& startObj(void) {
            doCont(false);
            return of(this->write('{'));
        }

        JsonWriter& startObj(const char* const name) {
            startItem(name);
            startObj();
            return *this;
        }

        JsonWriter& endObj(void) {
            this->cont = true;
            return of(this->write('}'));
        }

        JsonWriter& startArr(void) {
            doCont(false);
            return of(this->write('['));
        }

        JsonWriter& startArr(const char* const name) {
            startItem(name);
            startArr();
            return *this;
        }

        JsonWriter& endArr(void) {
            this->cont = true;
            return of(this->write(']'));
        }

        JsonWriter& startItem(const char* const name) {
            if(name) {
                this->writeValues('"', name, '"', ':');
                this->cont = false;
            }
            return *this;
        }

        JsonWriter& writeNullValue(void) {
            constexpr std::string_view NLL {"null"};
            doCont(true);
            this->write(NLL);
            return *this;
        }

        JsonWriter& writeValue(const char* const value) {
            if(value) {
                writeValues('"',value,'"');
            } else {
                writeNullValue();
            }
            return *this;
        }

        template<http::number_t T>
        JsonWriter& writeValue(T value) {
            return writeValues(value);
        } 

        JsonWriter& writeValue(const bool value) {
            constexpr std::string_view T {"true"};
            constexpr std::string_view F {"false"};
            return writeValues(value ? T : F);
        }

        template<typename ... Args>
        JsonWriter& writeValues(Args...args) {
            doCont(true);
            return of(Writer::writeValues(args...));
        }

        template<typename T>
        JsonWriter& writeItem(const char* const name, const T& value) {
            startItem(name);
            writeValue(value);
            return *this;
        }

        template<typename...Args>
        JsonWriter& writeArr(Args...args) {
            constexpr unsigned CNT = sizeof...(Args);
            startArr();
            if(CNT > 0) {
                if(ok()) {
                    writeValueList(args...);
                }
            }
            endArr();
            return *this;
        }

        private:

        JsonWriter& doCont(const bool c = true) {
            if(this->cont) {
                this->write(',');
            }
            this->cont = c;
            return *this;
        }

        template<typename T, typename...Args>
        JsonWriter& writeValueList(const T& a, const Args&...args) {
            doCont(true);
            this->write(a);
            if constexpr (sizeof...(Args) != 0) {
                if(ok()) {
                    writeValueList(args...);
                }
            }
            return *this;
        }

    };

}