#include "utils.h"

#include <string.h>
#include <stdio.h>

#include "mbedtls/sha256.h"

#include "mem_search.h"

#ifndef bswap_16
#define bswap_16(a) ((((uint16_t)(a) << 8) & 0xff00) | (((uint16_t)(a) >> 8) & 0xff))
#endif

#ifndef bswap_32
#define bswap_32(a) ((((uint32_t)(a) << 24) & 0xff000000) | \
                     (((uint32_t)(a) << 8) & 0xff0000) |    \
                     (((uint32_t)(a) >> 8) & 0xff00) |      \
                     (((uint32_t)(a) >> 24) & 0xff))
#endif

/*
 * General byte order swapping functions.
 */
#define bswap16(x) __bswap16(x)
#define bswap32(x) __bswap32(x)
#define bswap64(x) __bswap64(x)

uint32_t swab32(uint32_t v)
{
    return bswap_32(v);
}

// takes 80 bytes and flips every 4 bytes
void flip80bytes(void *dest_p, const void *src_p)
{
    uint32_t *dest = dest_p;
    const uint32_t *src = src_p;
    int i;

    for (i = 0; i < 20; i++)
        dest[i] = swab32(src[i]);
}

void flip64bytes(void *dest_p, const void *src_p)
{
    uint32_t *dest = dest_p;
    const uint32_t *src = src_p;
    int i;

    for (i = 0; i < 16; i++)
        dest[i] = swab32(src[i]);
}


// static inline uint32_t misalignment(const void* const ptr) {
//     return (uintptr_t)ptr & 0xf;
// }

static inline void* pplus(void* p, int i) {
    return (void*)((uintptr_t)p + i);
}

void flip32bytes(void* dst, const void* src) {
    uint32_t dummy;
    asm (
        "EE.LD.128.USAR.IP q0, %[src], 16" "\n"
        "EE.VLD.128.IP q1, %[src], 16" "\n"
        "EE.VLD.128.IP q2, %[src], 16" "\n"

        "EE.SRC.Q q0, q0, q1" "\n"
        "EE.SRC.Q q1, q1, q2" "\n"

        "EE.VUNZIP.16 q1, q0" "\n"
        "EE.VZIP.16 q0, q1" "\n"

        "EE.VUNZIP.8 q1, q0" "\n"
        "EE.VZIP.8 q0, q1" "\n"
        : [src] "+r" (src), "=m" (dummy)
        : "m" (*(const uint8_t(*)[32])src)
    );    

    // Endianness reversed. Now just get the result back into RAM:

    uintptr_t off = (((uintptr_t)dst + 15) & ~0xf) - (uintptr_t)dst;
    if(off == 0) {
        asm ( "ST.QR q0, %[dst], 0" "\n"
              "ST.QR q1, %[dst], 16" "\n"
            : "+m" (dummy), "=m" (*(uint8_t(*)[32])dst)
            : [dst] "r" (dst)
        );
    } else {
        if((off % 4) == 0) {
            if((off & 4) != 0) {
                uint32_t tmp;
                asm (
                    "EE.MOVI.32.A q0, %[tmp], 0" "\n"
                    "EE.SRCI.2Q q1, q0, (4-1)" "\n"
                    : [tmp] "=r" (tmp), "+m" (dummy), "=m" (*(uint8_t(*)[32])dst)
                    : [dst] "r" (dst)
                );
                *(uint32_t*)dst = tmp;
                dst = pplus(dst,4);
                asm (
                    "EE.VST.L.64.IP q0, %[dst], 8" "\n"
                    "EE.VST.H.64.IP q0, %[dst], 8" "\n"
                    "EE.VST.L.64.IP q1, %[dst], 8" "\n"
                    "EE.MOVI.32.A q1, %[tmp], 2" "\n"
                    // "S32I %[tmp], %[dst], 0" "\n"
                    : [dst] "+r" (dst), [tmp] "=r" (tmp),
                      "+m" (dummy), "=m" (*(uint8_t(*)[32])dst)
                    :
                );
                *(uint32_t*)dst = tmp;
            }  else 
            if((off & 8) != 0) {
                asm (
                    "EE.VST.L.64.IP q0, %[dst], 8" "\n"
                    "EE.VST.H.64.IP q0, %[dst], 8" "\n"
                    "EE.VST.L.64.IP q1, %[dst], 8" "\n"
                    "EE.VST.H.64.IP q1, %[dst], -(3*8)" "\n"
                    : "+m" (dummy), "=m" (*(uint8_t(*)[32])dst)
                    : [dst] "r" (dst)
                );
            }
        } else {
            // unaligned...
            uint32_t tmp;
            asm (
                "LOOP %[cnt], .Lend_%=" "\n"
                    "EE.MOVI.32.A q0, %[tmp], 0" "\n"
                    "EE.SRCI.2Q q1, q0, (4-1)" "\n"
                    "S32I %[tmp], %[dst], 0" "\n"
                    "ADDI %[dst], %[dst], 4" "\n"
                ".Lend_%=:"
                : [dst] "+r" (dst), [tmp] "=r" (tmp),
                   "+m" (dummy), "=m" (*(uint8_t(*)[32])dst)
                : [cnt] "r" (32/sizeof(uint32_t))
            );
        }
    }

}

// void flip32bytes(void *dest_p, const void *src_p)
// {
//     uint32_t *dest = dest_p;
//     const uint32_t *src = src_p;
//     int i;

//     for (i = 0; i < 8; i++)
//         dest[i] = swab32(src[i]);
// }

int hex2char(uint8_t x, char *c)
{
    if (x <= 9)
    {
        *c = x + '0';
    }
    else if (x <= 15)
    {
        *c = x - 10 + 'a';
    }
    else
    {
        return -1;
    }

    return 0;
}

static const char HEXCHARS[] = 
    {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

size_t bin2hex(const uint8_t *buf, size_t buflen, char *hex, size_t hexlen)
{
    if ((hexlen + 1) < buflen * 2)
    {
        return 0;
    }

    for (size_t i = 0; i < buflen; i++)
    {
        uint32_t b = buf[0];
        hex[0] = HEXCHARS[b >> 4];
        hex[1] = HEXCHARS[b & 0xf];
        hex += 2;
        buf += 1;
    }

    hex[0] = '\0';

    return 2 * buflen;
}

uint8_t hex2val(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    else if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    else if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    else
    {
        return 0;
    }
}

static inline unsigned hexchar2bin(const unsigned ch) {
    static const uint32_t MASK = ~(1<<5);
    static const uint32_t A_OFF = 'A'-10; // Mapping 'A' to 10
    static const uint32_t Z_OFF = '0' & MASK;


    int32_t x = (ch & ~(1<<5)); // Turns lower into upper case, and moves '0' to 0x10
    x = x - A_OFF; // Map 'A' to 10.
    // if not a letter, we subtracted too much, and compensate now:

    /* Logic: 
        if(x < 0) {
            x = x + A_OFF - Z_OFF;
        }
    */

    // With conditional move:
    // unsigned a = (x < 0) ? (A_OFF-Z_OFF) : 0;

    // Without conditional move:
    uint32_t a = (x >> 31) & (A_OFF-Z_OFF);
    return x+a;
}

static inline unsigned hex2u8(const char* hex) {
    return (hexchar2bin(hex[0]) << 4) | (hexchar2bin(hex[1]));
}

size_t hex2bin(const char *hex, uint8_t *bin, size_t bin_len)
{
    const unsigned hex_len = mem_findStrEnd(hex,bin_len*2) - hex;
    const unsigned byteCnt = (hex_len/2);

    uint8_t* const end = bin + byteCnt;
    while(bin < end) {
        *bin = hex2u8(hex);
        bin += 1;
        hex += 2;
    }
    if((hex_len & 1) != 0 && (byteCnt < bin_len)) {
        *bin = hexchar2bin(*hex) << 4;
        return byteCnt + 1;
    } else {
        return byteCnt;
    }
}

void print_hex(const uint8_t *b, size_t len,
               const size_t in_line, const char *prefix)
{
    size_t i = 0;
    const uint8_t *end = b + len;

    if (prefix == NULL) {
        prefix = "";
    } else
    {
        printf("%s", prefix);
    }

    
    while (b < end)
    {
        if (++i > in_line)
        {
            printf("\n%s", prefix);
            i = 1;
        }
        printf("%02X ", (uint8_t)*b++);
    }
    printf("\n");
    fflush(stdout);
}

char *double_sha256(const char *hex_string)
{
    size_t bin_len = mem_strlen(hex_string) / 2;
    uint8_t *bin = malloc(bin_len);
    hex2bin(hex_string, bin, bin_len);

    unsigned char first_hash_output[32], second_hash_output[32];

    mbedtls_sha256(bin, bin_len, first_hash_output, 0);
    mbedtls_sha256(first_hash_output, 32, second_hash_output, 0);

    free(bin);

    char *output_hash = malloc(64 + 1);
    bin2hex(second_hash_output, 32, output_hash, 65);
    return output_hash;
}

void double_sha256_bin(const uint8_t *data, const size_t data_len, Hash_t* const out_hash)
{
    mbedtls_sha256(data, data_len, out_hash->u8, 0);
    mbedtls_sha256(out_hash->u8, 32, out_hash->u8, 0);
}

void single_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t *dest)
{
    // Initialize SHA256 context
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0);

    // Compute first SHA256 hash of header
    mbedtls_sha256_update(&sha256_ctx, data, data_len);
    // unsigned char hash[32];
    mbedtls_sha256_finish(&sha256_ctx, dest);
}

void midstate_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t *dest)
{
    mbedtls_sha256_context midstate;

    // Calculate midstate
    mbedtls_sha256_init(&midstate);
    mbedtls_sha256_starts(&midstate, 0);
    mbedtls_sha256_update(&midstate, data, 64);

    // memcpy(dest, midstate.state, 32);
     flip32bytes(dest, midstate.state);
}

void swap_endian_words(const char *hex_words, uint8_t *output)
{
    // size_t hex_length = strlen(hex_words);
    uint32_t hex_length = mem_findStrEnd(hex_words,2047) - hex_words;
    if (hex_length % 8 == 0) {

        // size_t binary_length = hex_length / 2;

        uint8_t* const end = output + hex_length / 2;
        while(output < end) {

        // for (size_t i = 0; i < binary_length; i += 4)
        // {
            output[3] = hex2u8(hex_words + 0);
            output[2] = hex2u8(hex_words + 2);
            output[1] = hex2u8(hex_words + 4);
            output[0] = hex2u8(hex_words + 6);
            output += 4;
            hex_words += 8;
            
            
            // for (int j = 0; j < 4; j++)
            // {
            //     unsigned int byte_val;
            //     sscanf(hex_words + (i + j) * 2, "%2x", &byte_val);
            //     output[i + (3 - j)] = byte_val;
            // }
        }
    } else {
        fprintf(stderr, "Must be 4-byte word aligned\n");
        exit(EXIT_FAILURE);
    }

}

void reverse_bytes(uint8_t *data, size_t len)
{
    for (int i = 0; i < len / 2; ++i)
    {
        uint8_t temp = data[i];
        data[i] = data[len - 1 - i];
        data[len - 1 - i] = temp;
    }
}

// // static const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;
// static const double bits192 = 6277101735386680763835789423207666416102355444464034512896.0;
// static const double bits128 = 340282366920938463463374607431768211456.0;
// static const double bits64 = 18446744073709551616.0;

// /* Converts a little endian 256 bit value to a double */
// double le256todouble(const void *target)
// {
//     uint64_t *data64;
//     double dcut64;

//     data64 = (uint64_t *)(target + 24);
//     dcut64 = *data64 * bits192;

//     data64 = (uint64_t *)(target + 16);
//     dcut64 += *data64 * bits128;

//     data64 = (uint64_t *)(target + 8);
//     dcut64 += *data64 * bits64;

//     data64 = (uint64_t *)(target);
//     dcut64 += *data64;

//     return dcut64;
// }

void prettyHex(const unsigned char *buf, int len)
{
    int i;
    printf("[");
    for (i = 0; i < len - 1; i++)
    {
        printf("%02X ", buf[i]);
    }
    printf("%02X]", buf[len - 1]);
}

uint32_t flip32(uint32_t val)
{
    uint32_t ret = 0;
    ret |= (val & 0xFF) << 24;
    ret |= (val & 0xFF00) << 8;
    ret |= (val & 0xFF0000) >> 8;
    ret |= (val & 0xFF000000) >> 24;
    return ret;
}