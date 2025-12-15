/*
 * MIT License
 *
 * Copyright (c) 2010 Serge Zaitsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#pragma once

#include <stddef.h>
#include <cstdint>
#include <span>
#include <string_view>
#include <compare>

#ifdef JSMN_STATIC
	#define JSMN_API static
#else
	#define JSMN_API extern
#endif


namespace jsmn {

	#ifdef JSMN_SMALL
		using ix_t = uint16_t;
	#else
		using ix_t = unsigned int;
	#endif

	constexpr std::string_view VALUE_TRUE {"true"};
	constexpr std::string_view VALUE_FALSE {"false"};
	constexpr std::string_view VALUE_NULL {"null"};
	/**
	 * JSON type identifier. Basic types are:
	 * 	o Object
	 * 	o Array
	 * 	o String
	 * 	o Other primitive: number, boolean (true/false) or null
	 */
	typedef enum : uint8_t {
		JSMN_UNDEFINED = 0,
		JSMN_OBJECT = 1 << 0,
		JSMN_ARRAY = 1 << 1,
		JSMN_STRING = 1 << 2,
		JSMN_PRIMITIVE = 1 << 3,
		JSMN_KEY = 1 << 4
	} jsmntype_t;



	enum jsmnerr {
		/* Not enough tokens were provided */
		JSMN_ERROR_NOMEM = -1,
		/* Invalid character inside JSON string */
		JSMN_ERROR_INVAL = -2,
		/* The string is not a full JSON packet, more bytes expected */
		JSMN_ERROR_PART = -3
	};




	constexpr ix_t JSMN_INV_IX = (ix_t)-1;

	static constexpr bool jsmn_is_valid(const unsigned ix) {
		  return (ix != JSMN_INV_IX);
	}

	/**
	 * JSON token description.
	 * type		type (object, array, string etc.)
	 * start	start position in JSON data string
	 * end		end position in JSON data string
	 */
	struct jsmntok_t {
		ix_t start {0};
		ix_t end {0};
		ix_t size {0};
		jsmntype_t type {JSMN_UNDEFINED};
	#ifdef JSMN_PARENT_LINKS
		ix_t parent {JSMN_INV_IX};
	#endif
		constexpr jsmntok_t() = default;
		constexpr jsmntok_t(const jsmntype_t type, const unsigned start, const unsigned end) :
			start {(ix_t)start},
			end {(ix_t)end},
			type {type}
		{

		}

		constexpr bool operator ==(const jsmntok_t& other) const {
			return this->start == other.start && this->end == other.end;
		}

		constexpr bool operator <(const jsmntok_t& other) const {
			return this->endsBefore(other.start);
		}

		constexpr bool endsBefore(const ix_t pos) const {
			return this->end < pos;
		}

		constexpr bool is(const jsmntype_t type) const {
			return this->type == type;
		}

		constexpr bool isStr() const {
			return is(JSMN_STRING);
		}
		constexpr bool isKey() const {
			return is(JSMN_KEY);
		}
		constexpr bool isObj() const {
			return is(JSMN_OBJECT);
		}
		constexpr bool isArr() const {
			return is(JSMN_ARRAY);
		}
		constexpr bool isPrim() const {
			return is(JSMN_PRIMITIVE);
		}
		constexpr bool isValue() const {
			  return isStr() || isPrim();
		}
		constexpr bool isEmpty() const {
			return start == end;
		}
		constexpr bool isContainer() const {
			  return isObj() || isArr();
		}
		constexpr bool isOpen() const {
			return jsmn_is_valid(start) && !jsmn_is_valid(end);
		}
		constexpr std::size_t length() const {
			return this->end - this->start;
		}
	};

	// static_assert(sizeof(jsmntok_t) == 0);

	using toklist_v = std::span<jsmntok_t>;
	using ctoklist_v = std::span<const jsmntok_t>;

	namespace impl {
		/**
		 * JSON parser. Contains an array of token blocks available. Also stores
		 * the string being parsed now and current position in that string.
		 */
		struct jsmn_parser {
			unsigned int pos {0};     /* offset in the JSON string */
			unsigned int toknext {0}; /* next token to allocate */
			ix_t toksuper {JSMN_INV_IX};  /* superior token node, e.g. parent object or array */


			/**
			 * Parse JSON string and fill tokens.
			 */
			constexpr int parse(const std::string_view& json, const toklist_v& tokens) {
				// int r;
				// int i;
				// jsmntok_t* token;
				unsigned int count = this->toknext;

				// if(len == JSMN_INV_IX) {
				//   len = JSMN_INV_IX-1;
				// }

				char ch;
				for (; this->pos < json.size() && (ch = json[this->pos]) != '\0'; this->pos += 1) {
				// char c;
				// jsmntype_t type;

				// c = js[parser->pos];
				switch (ch) {
				case '{':
				case '[': {
					count++;
					if (tokens.empty()) {
					break;
					}
					jsmntok_t* const token = alloc_token(tokens);
					if (token == NULL) [[unlikely]] {
					return (int)JSMN_ERROR_NOMEM;
					}
					if (jsmn_is_valid(this->toksuper)) {
					jsmntok_t& t = tokens[this->toksuper];
			#ifdef JSMN_STRICT
					/* In strict mode an object or array can't become a key */
					if (t.type == JSMN_OBJECT) [[unlikely]] {
						return (int)JSMN_ERROR_INVAL;
					}
			#endif
					t.size += 1;
			#ifdef JSMN_PARENT_LINKS
					token->parent = this->toksuper;
			#endif
					}
					token->type = (ch == '{' ? JSMN_OBJECT : JSMN_ARRAY);
					token->start = this->pos;
					this->toksuper = this->toknext - 1;
					break;
				}
				case '}':
				case ']': {
					if (tokens.empty()) {
					break;
					}
					jsmntype_t type = (ch == '}' ? JSMN_OBJECT : JSMN_ARRAY);
			#ifdef JSMN_PARENT_LINKS
					if (this->toknext == 0 || !jsmn_is_valid(this->toknext)) [[unlikely]] {
					return (int)JSMN_ERROR_INVAL;
					}
					{
					jsmntok_t* token = &tokens[this->toknext - 1];
					// while(jsmn_is_valid(token->parent) && !jsmn_is_open()) ...
					for (;;) {
						// if (jsmn_is_valid(token->start) && !jsmn_is_valid(token->end)) {
						if(token->isOpen()) {
						if (token->type != type) [[unlikely]] {
							return (int)JSMN_ERROR_INVAL;
						}
						token->end = this->pos + 1;
						this->toksuper = token->parent;
						break;
						}
						if (!jsmn_is_valid(token->parent)) {
						if (token->type != type || !jsmn_is_valid(this->toksuper)) [[unlikely]] {
							return (int)JSMN_ERROR_INVAL;
						}
						break;
						}
						token = &tokens[token->parent];
					}
					}
			#else
					int i;
					for (i = this->toknext - 1; i >= 0; i--) {
					jsmntok_t& t = tokens[i];
					// if (jsmn_is_valid(token->start) && !jsmn_is_valid(token->end)) {
					if(t.isOpen()) {
						if (t.type != type) [[unlikely]] {
						return (int)JSMN_ERROR_INVAL;
						}
						this->toksuper = JSMN_INV_IX;
						t.end = this->pos + 1;
						break;
					}
					}
					/* Error if unmatched closing bracket */
					if (i < 0) [[unlikely]] {
					return (int)JSMN_ERROR_INVAL;
					}
					for (; i >= 0; i--) {
					jsmntok_t& t = tokens[i];
					// if (jsmn_is_valid(token->start) && !jsmn_is_valid(token->end)) {
					if(t.isOpen()) {
						this->toksuper = i;
						break;
					}
					}
			#endif
					break;
				}
				case '\"': {
					int r = parse_string(json,tokens);
					if (r < 0) [[unlikely]] {
					return r;
					}
					count++;
					if (jsmn_is_valid(this->toksuper) && !tokens.empty()) {
					tokens[this->toksuper].size++;
					}
					break;
				}
				case '\t':
				case '\r':
				case '\n':
				case ' ':
					break;
				case ':': {
					if(!tokens.empty()) {
						jsmntok_t* prev_tok = NULL;
						if(this->toknext >= 1 && jsmn_is_valid(this->toknext)) {
							prev_tok = &tokens[this->toknext-1];
						}
						if((prev_tok != NULL) && (prev_tok->isStr())) {
							prev_tok->type = JSMN_KEY;
						}
			#ifdef JSMN_STRICT
						else {
							// A : must have been preceded by a key, i.e. a string.
							return (int)JSMN_ERROR_INVAL
						}
			#endif
					}

					// if(parser->toknext != 0 && tokens != NULL) {
					//     tokens[parser->toknext-1].type = JSMN_KEY;
					// }
					this->toksuper = this->toknext - 1;
					break;
					}
				case ',': {
					if (!tokens.empty() && jsmn_is_valid(this->toksuper) &&
						!tokens[this->toksuper].isContainer() ) {
						// tokens[this->toksuper].type != JSMN_ARRAY &&
						// tokens[this->toksuper].type != JSMN_OBJECT) {
			#ifdef JSMN_PARENT_LINKS
					this->toksuper = tokens[this->toksuper].parent;
			#else
					for (int i = this->toknext - 1; i >= 0; i--) {
						// if (tokens[i].type == JSMN_ARRAY || tokens[i].type == JSMN_OBJECT) {
						if(tokens[i].isContainer()) {
						// if (jsmn_is_valid(tokens[i].start) && !jsmn_is_valid(tokens[i].end)) {
						if(tokens[i].isOpen()) {
							this->toksuper = i;
							break;
						}
						}
					}
			#endif
					}
					break;
				}
			#ifdef JSMN_STRICT
				/* In strict mode primitives are: numbers and booleans */
				case '-':
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
				case 't':
				case 'f':
				case 'n':
					/* And they must not be keys of the object */
					if (!tokens.empty() && jsmn_is_valid(this->toksuper)) {
					const jsmntok_t *t = &tokens[this->toksuper];
					if (t->type == JSMN_OBJECT ||
						(t->type == JSMN_STRING && t->size != 0)) {
						return (int)JSMN_ERROR_INVAL;
					}
					}
			#else
				/* In non-strict mode every unquoted value is a primitive */
				default:
			#endif
					{
					int r = parse_primitive(json,tokens);
					if (r < 0) {
					return r;
					}
					count++;
					if (jsmn_is_valid(this->toksuper) && !tokens.empty()) {
					tokens[this->toksuper].size++;
					}
					break;
				}
			#ifdef JSMN_STRICT
				/* Unexpected char in strict mode */
				default:
					return (int)JSMN_ERROR_INVAL;
			#endif
				}
				}

				if (!tokens.empty()) {
				for (int i = this->toknext - 1; i >= 0; i--) {
					/* Unmatched opened object or array */
					if (tokens[i].isOpen()) {
					// jsmn_is_valid(tokens[i].start) && !jsmn_is_valid(tokens[i].end)) {
					return (int)JSMN_ERROR_PART;
					}
				}
				}

				return count;
			}


			private:

			static constexpr bool jsmn_is_open(const jsmntok_t& tok) {
				return jsmn_is_valid(tok.start) && !jsmn_is_valid(tok.end);
			}

			constexpr jsmntok_t* alloc_token(const toklist_v& tokens) {
				jsmntok_t* tok {nullptr};
				if (this->toknext < tokens.size()) {
					jsmntok_t& t = tokens[this->toknext];
					this->toknext += 1;
					t.start = JSMN_INV_IX;
					t.end = JSMN_INV_IX;
					t.size = 0;
				#ifdef JSMN_PARENT_LINKS
					t.parent = JSMN_INV_IX;
				#endif
					tok = &t;
				}
				return tok;
			}

			/**
			 * Fills next available token with JSON primitive.
			 */
			int parse_primitive(const std::string_view& json, const jsmn::toklist_v& tokens) {
				jsmn::jsmntok_t* token;
				unsigned int start;

				start = this->pos;

				char ch;
				for (; this->pos < json.size() && (ch = json[this->pos]) != '\0'; this->pos += 1) {
				switch (ch) {
				#ifndef JSMN_STRICT
					/* In strict mode primitive must be followed by "," or "}" or "]" */
					case ':':
				#endif
					case '\t':
					case '\r':
					case '\n':
					case ' ':
					case ',':
					case ']':
					case '}':
					goto found;
					default:
								/* to quiet a warning from gcc*/
					break;
				}
				if (ch < 32 || ch >= 127) {
					this->pos = start;
					return (int)JSMN_ERROR_INVAL;
				}
				}
			#ifdef JSMN_STRICT
				/* In strict mode primitive must be followed by a comma/object/array */
				this->pos = start;
				return (int)JSMN_ERROR_PART;
			#endif

			found:
				if (tokens.empty()) {
				this->pos -= 1;
				return 0;
				}
				token = alloc_token(tokens);
				if (token == NULL) {
				this->pos = start;
				return (int)JSMN_ERROR_NOMEM;
				}
				*token = jsmntok_t {JSMN_PRIMITIVE, start, this->pos};
				// jsmn_fill_token(token, JSMN_PRIMITIVE, start, parser->pos);
			#ifdef JSMN_PARENT_LINKS
				token->parent = this->toksuper;
			#endif
				this->pos--;
				return 0;
			}

			/**
			 * Fills next token with JSON string.
			 */
			int parse_string(const std::string_view& json, const toklist_v& tokens) {
				jsmntok_t *token;

				unsigned int start = this->pos;

				/* Skip starting quote */
				this->pos += 1;

				char ch;

				for (; this->pos < json.size() && (ch = json[this->pos]) != '\0'; this->pos += 1) {
				// char c = js[parser->pos];

				/* Quote: end of string */
				if (ch == '\"') {
					if (tokens.empty()) {
					return 0;
					}
					token = alloc_token(tokens);
					if (token == NULL) {
					this->pos = start;
					return (int)JSMN_ERROR_NOMEM;
					}
					*token = jsmntok_t { JSMN_STRING, start+1, this->pos};
					// jsmn_fill_token(token, JSMN_STRING, start + 1, parser->pos);
			#ifdef JSMN_PARENT_LINKS
					token->parent = this->toksuper;
			#endif
					return 0;
				}

				/* Backslash: Quoted symbol expected */
				if (ch == '\\' && (this->pos + 1) < tokens.size()) {
					// parser->pos++;
					this->pos += 1;
					switch (json[this->pos]) {
					/* Allowed escaped symbols */
					case '\"':
					case '/':
					case '\\':
					case 'b':
					case 'f':
					case 'r':
					case 'n':
					case 't':
						break;
					/* Allows escaped symbol \uXXXX */
					case 'u':
						this->pos += 1;
						for (unsigned int i = 4; i != 0 && this->pos < tokens.size() && (ch = json[this->pos]) != '\0';
							--i) {
						/* If it isn't a hex character we have an error */
						if (!((ch >= 48 && ch <= 57) ||   /* 0-9 */
								(ch >= 65 && ch <= 70) ||   /* A-F */
								(ch >= 97 && ch <= 102))) [[unlikely]] { /* a-f */
							this->pos = start;
							return (int)JSMN_ERROR_INVAL;
						}
						this->pos++;
						}
						this->pos--;
						break;
					/* Unexpected symbol */
					default:
						this->pos = start;
						return (int)JSMN_ERROR_INVAL;
					}
				}
				}
				this->pos = start;
				return (int)JSMN_ERROR_PART;
			}

		};

	} // namespace impl




    constexpr std::string_view typeStr(const jsmntok_t& tok) {
        switch(tok.type) {
            case jsmntype_t::JSMN_UNDEFINED : return "undef";
            case jsmntype_t::JSMN_OBJECT : return "obj";
            case jsmntype_t::JSMN_ARRAY : return "arr";
            case jsmntype_t::JSMN_STRING : return "str";
            case jsmntype_t::JSMN_PRIMITIVE : return "prim";
            case jsmntype_t::JSMN_KEY : return "key";
            default: return "unknown";
        }
    }

    struct Tix_t {
        static constexpr bool valid(const unsigned ix) {
            return jsmn_is_valid(ix);
        }

        ix_t val {JSMN_INV_IX};
        constexpr Tix_t() = default;
        constexpr Tix_t(const Tix_t&) = default;
        constexpr Tix_t(const ix_t ix) : val {ix} {

        }

        constexpr bool valid() const {
            return valid(val);
        }

        constexpr explicit operator bool() const {
            return valid();
        }

        constexpr operator ix_t() const {
            return val;
        }

        constexpr Tix_t& operator =(const Tix_t&) = default;
        constexpr Tix_t& operator =(const ix_t ix) {
            val = ix;
            return *this;
        }
        constexpr auto operator <=>(const Tix_t& other) const {
            return this->val <=> other.val;
        }
        constexpr auto operator <=>(const int ix) const {
            return (int)this->val <=> ix;
        }
        constexpr Tix_t& operator ++() {
            val += 1;
            return *this;
        }
        constexpr Tix_t operator ++(int) {
            Tix_t prev {*this};
            val += 1;
            return prev;
        }
        constexpr Tix_t& operator --() {
            val -= 1;
            return *this;
        }
        constexpr Tix_t operator --(int) {
            Tix_t prev {*this};
            val -= 1;
            return prev;
        }
        constexpr Tix_t& operator +=(const int x) {
            this->val += x;
            return *this;
        }
        constexpr Tix_t& operator -=(const int x) {
            this->val -= x;
            return *this;
        }

        friend
        constexpr Tix_t operator +(const Tix_t& a, const int b) {
            return Tix_t {(ix_t)(a.val + b)};
        }

        friend
        constexpr Tix_t operator -(const Tix_t& a, const int b) {
            return Tix_t {(ix_t)(a.val - b)};
        }
    };

    struct Parsed {
        std::string_view json {};
        ctoklist_v tokens {};
        constexpr Parsed() = default;
        constexpr Parsed(const Parsed&) = default;
        constexpr Parsed(Parsed&&) = default;
        constexpr Parsed(const std::string_view& json, const ctoklist_v& tokens) :
            json {json},
            tokens {tokens}
        {

        }
        constexpr Parsed& operator =(const Parsed&) = default;
        constexpr unsigned size() const {
            return tokens.size();
        }

		constexpr Tix_t first(void) const {
			if(!tokens.empty()) {
				return 0;
			} else {
				return JSMN_INV_IX;
			}
		}

		constexpr Tix_t last(void) const {
			if(!tokens.empty()) {
				return tokens.size()-1;
			} else {
				return JSMN_INV_IX;
			}
		}

        constexpr Tix_t getAt(const unsigned pos, const unsigned startIx = 0, const unsigned endIx = -1) const {
            const unsigned max = tokens.size();
            if(startIx < max && endIx >= startIx ) {
                unsigned lo = startIx;
                unsigned hi = endIx < max ? endIx : max;
                unsigned mid = (lo+hi) / 2;
                do {
                    const unsigned strt = tokens[mid].start;
                    const unsigned end = tokens[mid].end;
                    if(pos < strt) {
                        hi = mid-1;
                    } else
                    if(pos > end) {
                        lo = mid;
                    } else {
                        break;
                    }
                    mid = (lo+hi) / 2;
                } while(mid > lo);
// TODO:
                if((mid+1) < max && tokens[mid].end < pos) {
                    ++mid;
                }
                if(mid < max) {
                    return mid;
                }
            } 
            
            // Not found:
            return Tix_t {};
        }

        protected:

        constexpr bool valid(const unsigned ix) const {
            return Tix_t::valid(ix) && (ix < tokens.size());
        }

		constexpr std::string_view substr(std::size_t start, std::size_t len) const {
			// if((start+len) < json.size()) {
				return std::string_view {json.data() + start, len};
			// } else {
			// 	return std::string_view {};
			// }
		}
    };

    struct Tkn : public Parsed {
        Tix_t tix {};
        constexpr Tkn() = default;
        constexpr Tkn(const Tkn&) = default;
        constexpr Tkn(Tkn&&) = default;
        constexpr Tkn(const Parsed& parsed, const ix_t tix) :
            Parsed {parsed},
            tix {tix} {

        }
		constexpr Tkn(const Parsed& parsed) :
			Parsed {parsed},
			tix {parsed.first()} {

		}

        constexpr Tkn& operator =(const Tkn&) = default;

		constexpr auto operator <=>(const std::string_view& value) const {
			return this->str() <=> value;
		}

        constexpr Tkn& operator ++() {
            this->tix += 1;
            return *this;
        }

        constexpr Tkn operator ++(int) {
            const Tkn prev {*this};
            this->tix += 1;
            return prev;
        }

        constexpr Tkn& operator --() {
            this->tix -= 1;
            return *this;
        }

        constexpr Tkn operator --(int) {
            const Tkn prev {*this};
            this->tix -= 1;
            return prev;   
        }

        friend
        constexpr Tkn operator +(const Tkn& a, const int b) {
            return Tkn {a, a.tix + b};
        }

        friend
        constexpr Tkn operator -(const Tkn& a, const int b) {
            return Tkn {a, a.tix - b};
        }

        constexpr auto operator <=>(const Tkn& other) const {
            return this->tix <=> other.tix;
        }

        constexpr bool valid() const {
            return Parsed::valid(this->tix);
        }

        constexpr operator bool() const {
            return valid();
        }

		constexpr std::size_t length() const {
			return token().length();
		}

        constexpr jsmntype_t type() const {
            return token().type;
        }
        constexpr bool isObj() const {
            return type() == JSMN_OBJECT;
        }
        constexpr bool isArr() const {
            return type() == JSMN_ARRAY;
        }
        constexpr bool isKey() const {
            return type() == JSMN_KEY;
        }
        constexpr bool isPrim() const {
            return type() == JSMN_PRIMITIVE;
        }
        constexpr bool isStr() const {
            return type() == JSMN_STRING;
        }
        constexpr bool isValue() const {
            return isStr() || isPrim();
        }
        constexpr bool isContainer() const {
            return isObj() || isArr();
        }
		constexpr bool isNull() const {
			return isPrim() && str() == VALUE_NULL;
		}
		constexpr bool isEmptyStr() const {
			return isStr() && token().isEmpty();
		}
		constexpr bool isBool() const {
			bool b = false;
			if(isPrim()) {
				const std::string_view s = this->str();
				b = s.size() > 0 && (s[0] == 't' || s[0] == 'f');
			}
			return b;
		}
		constexpr bool isTrue() const {
			return isPrim() && str() == VALUE_TRUE;
		}
		constexpr bool isFalse() const {
			return isPrim() && str() == VALUE_FALSE;
		}
		
        constexpr bool contains(const Tkn& t) const {
            return this->valid() && t.valid() &&
                   this->token().start <= t.token().start &&
                   this->token().end >= t.token().end;
        }

        constexpr Tkn getChildren() const {
            if(isContainer()) {
                const Tkn nxt = this->getNext();
                if(nxt && (nxt.token().start <= this->token().end)) {
                    // Only search if container not empty.

                    const unsigned end = this->token().end;
                    const unsigned ix = Parsed::getAt(end,this->tix);

                    ctoklist_v tkns = Parsed::valid(ix) ?
                        this->tokens.subspan(0,ix) :
                        this->tokens;
                    return Tkn { Parsed {this->json, tkns }, this->tix+1 };
                }
            }
            // Not a container, or an empty one
            return Tkn {*this,JSMN_INV_IX};
            
        }

        constexpr unsigned countChildren() const {
            if(isContainer()) {
                // const jsmntok_t& tok = token();
                const unsigned end = token().end;
                // unsigned ix = this->tix+1;
                // const unsigned eix = size();
                // while (ix < eix && this->tokens[ix])
                unsigned cnt = 0;
                Tkn t = getNext();
                while (t && t.token().start < end) {
                    ++cnt;
                    t.nextSibling();
                }
                return cnt;
            } else {
                return 0;
            }
        }

        constexpr const char* data() const {
            return &Parsed::json[token().start];
        }

        constexpr std::size_t size() const {
            return token().end - token().start;
        }

        constexpr std::string_view str() const {
            if(valid()) {
				// return std::string_view {Parsed::json.data() + token().start, size()};
				// return std::string_view {data(), size()};
                // return Parsed::json.substr(token().start, size());
				return Parsed::substr(token().start, token().length());
            } else {
                return std::string_view {};
            }
        }

        constexpr operator std::string_view() const {
            return str();
        }

        constexpr bool hasNext() const {
            return (tix.val+1) < Parsed::tokens.size();
        }

        constexpr Tkn getNext() const {
            return Tkn {*this, (ix_t)(this->tix+1)};
        }

        constexpr Tkn& next() {
            this->tix += 1;
            return *this;
        }

        /**
         * @brief Skips over the element represented by this token:
         * If this token is a key, the associated value (of any type) is skipped over.
         * If this token is an object or an array, the object or array is skipped over.
         * Otherwise, this token advances to the next token.
         * 
         * @return this token
         */
        constexpr Tkn& nextSibling() {
            if(valid()) {
                const jsmntok_t& tok = token();
                if(tok.type == JSMN_KEY) {
                    return next().nextSibling();
                } else
                if((tok.type == JSMN_ARRAY || tok.type == JSMN_OBJECT) && tok.size > 0) {

                    const unsigned end = tok.end;
					// if((end - tok.start) < 128) { // Just some arbitrary threshold...
						const ctoklist_v sub = this->tokens.subspan(this->tix+1);
						unsigned cnt = 1;
						for(const jsmntok_t& t : sub) {
							if(t.start <= end) {
								++cnt;
							} else {
								break;
							}
						}
						this->tix += cnt;
					// }
					//  else {
					// 	this->tix = Parsed::getAt(end, this->tix.val+1);
					// }
					return *this;
                } else {
					return next();
				}
            } else {
				// If invalid, don't advance; just return this invalid token.
            	return *this;
			} 
        }

        constexpr Tkn getNextSibling() const {
            return Tkn {*this}.nextSibling();
        }

        constexpr Tkn getAt(const unsigned pos) const {
            return Tkn {*this, Parsed::getAt(pos,this->tix)};
        }

        constexpr const jsmntok_t& token() {
            return Parsed::tokens[this->tix];
        }

        constexpr const jsmntok_t& token() const {
            return Parsed::tokens[this->tix];
        }
    };

    struct ArrTkn : public Tkn {
        using Tkn::Tkn;
        constexpr ArrTkn(const Tkn& t) : Tkn {t} {

        }
        constexpr ArrTkn(const ArrTkn&) = default;

        constexpr bool valid() const {
            return Tkn::valid() && Tkn::isArr();
        }

        constexpr operator bool() const {
            return valid();
        }
    };

    struct TknIter : public Tkn {
        public:
        constexpr TknIter() = default;
        constexpr TknIter(const TknIter&) = default;
        constexpr TknIter(const Tkn& tkn) : Tkn {tkn} {

        }
        constexpr TknIter& operator =(const Tkn& t) {
            Tkn::operator =(t);
            return *this;
        }
        constexpr auto operator <=>(const Tkn& t) const {
            return Tkn::tix <=> t.tix;
        }

        constexpr TknIter& operator ++() {
            ++Tkn::tix;
            return *this;
        }

        constexpr TknIter operator ++(int) {
            TknIter prev {*this};
            ++Tkn::tix;
            return prev;
        }

        constexpr TknIter& operator +=(const int i) {
            Tkn::tix += i;
            return *this;
        }

        constexpr Tkn& operator*() {
            return *this;
        }
        constexpr const Tkn& operator*() const {
            return *this;
        }
    };

    struct ParseResult : public Parsed {
        int result {0};
        constexpr ParseResult() = default;
        constexpr ParseResult(const ParseResult&) = default;
        constexpr ParseResult(ParseResult&&) = default;
        constexpr ParseResult(const std::string_view& json, int r) :
            Parsed {json,std::span<jsmntok_t> {}},
            result {r} {

        }
        constexpr ParseResult(const std::string_view& json, const std::span<jsmntok_t>& tokens) :
            Parsed {json, tokens},
			result { (int)tokens.size() } {

            }
        
        constexpr bool valid() const {
            return this->result > 0;
        }

        constexpr operator bool() const {
            return valid();
        }

        constexpr Tkn operator[](const unsigned i) const {
            return Tkn {*this,(ix_t)i};
        }

        constexpr Tkn first() const {
            return Tkn {*this,0};
        }

        constexpr TknIter begin() {
            return TknIter {first()};
        }

        constexpr TknIter end() {
            return TknIter {Tkn {*this,(ix_t)this->size()}};
        }
    };


    class Parser {
        public:
        std::span<jsmntok_t> tokens {};
        constexpr Parser() = default;
        constexpr Parser(const Parser&) = default;
        constexpr Parser(Parser&&) = default;
        constexpr Parser(const std::span<jsmntok_t>& tokens) :
            tokens {tokens}
        {

        }
        ParseResult parse(const std::string_view& json) {
            jsmn::impl::jsmn_parser p {};
            // jsmn_parser p {};
            // jsmn_init(&p);
            // int r = jsmn_parse(&p,json.data(),json.size(),tokens.data(),tokens.size());
            int r = p.parse(json,this->tokens);
            if(r > 0) {
                return ParseResult {json, this->tokens.subspan(0,r)};
            } else {
                return ParseResult {json, r};
            }
        }
    };


} // namespace jsmn
