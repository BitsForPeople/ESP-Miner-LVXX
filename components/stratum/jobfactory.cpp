#include "jobfactory.h"
#include "jobfactory.hpp"
#include "esp_log.h"
#include <cstdio>
// #include "global_state.h"




#include <cstdint>
#include <array>

static constexpr unsigned BLOCK_WIDTH = 6;
static constexpr unsigned BLOCK_HEIGHT = 6;

/**
 * @brief Calculates the average of a block of 8-bit values.
 * 
 * @param mem       pointer to the first value ("top-left" corner) of the block 
 * @param rowlen    size of a data row in bytes;
 *                  the 2nd row of the block starts at (mem + rowlen), the 3rd at
 *                  (mem + 2*rowlen) &c.
 * @return          the average of (BLOCK_WIDTH*BLOCK_HEIGHT) values from the block
 */
static inline uint32_t get_block_avg(const uint8_t* mem, const uint32_t rowlen);

/**
 * @brief Type to hold two block averages of one block and the next block "below" it (BLOCK_HEIGHT rows down).
 * 
 */
struct DoubleVAvg {
    uint32_t l_curr {};
    uint32_t l_down {};

    constexpr DoubleVAvg(const std::array<uint32_t,2>& arr) :
        l_curr {arr[0]},
        l_down {arr[1]}
    {

    }
};

/**
 * @brief Calculates the averages of two vertically adjacent blocks.
 * This avoids some overhead compared to calling \c get_block_avg() twice and
 * is therefore faster when both blocks need to be looked at anyway.
 * 
 * @param mem       pointer to the "top-left" corner of the "upper" block 
 * @param rowlen    size of a data row in bytes;
 * @return a \c DoubleVAvg holding the two averages of the two blocks
 */
static inline DoubleVAvg get_double_block_avg(const uint8_t* mem, const uint32_t rowlen);


namespace detail {

    // Just a type to wrap a 16-byte array that is 16-byte aligned in memory.
    struct alignas(16) Vec_t {
        std::array<uint8_t,16> v {};
    };

    namespace {
        // Build a vector of BLOCK_WIDTH ones and the rest zeros
        // This is used to accumulate only the pixels of interest, "masking" all others.
        // (Pixels of interest are multiplied by 1, the others are multiplied by 0.)
        static constexpr Vec_t makeBlockMaskVector(void) {
            Vec_t vec {};
            for(unsigned i = 0; i < BLOCK_WIDTH; ++i) {
                vec.v[i] = 1;
            }
            return vec;
        }
    } // namespace

    static constexpr Vec_t BLOCK_MASK = makeBlockMaskVector();

    /**
     * @brief Implements the actual SIMD-powered average calculation.
     * 
     * @tparam VBLOCK_CNT the number of vertically adjacent blocks to process
     * @param mem pointer to the "top-left" corner of the top-most block
     * @param rowlen size of a row in bytes
     * @return an array of \c VBLOCK_CNT averages calculated from the \c VBLOCK_CNT blocks
     */
    template<unsigned VBLOCK_CNT = 1>
    static inline std::array<uint32_t,VBLOCK_CNT> blkavg(const uint8_t* mem, const uint32_t rowlen) {

        static_assert(BLOCK_HEIGHT > 0);
        static_assert(BLOCK_WIDTH > 0);
        static_assert(BLOCK_WIDTH <= 16);

        /*
            Q registers used below:
            (q1:q0) - load unaligned data from mem
            q2 - temporary to hold aligned data
            q7 - initialized to BLOCK_MASK, i.e. q7[n] := BLOCK_MASK.v[n]
        */

        // Dummy to make the compiler aware of dependencies between the asm's
        // This way, we don't need to use "asm volatile", and the compiler can optimize more.
        uint32_t dummy;

        // Memory barrier for the compiler:
        asm ("" : "=m" (dummy) : "m" (*(const uint8_t(*)[rowlen*VBLOCK_CNT*BLOCK_HEIGHT])mem));

        // q7[n] := BLOCK_MASK[n]
        asm (
            "LD.QR q7, %[mask], 0" "\n"
            : "+m" (dummy)
            : [mask] "r" (BLOCK_MASK.v.data()), "m" (BLOCK_MASK.v)
        );

        const uint32_t skip = rowlen - 16;
        // Load the first 32 bytes from *mem into (q1:q0) and make mem point to the 
        // data in the next row.
        asm (
            "EE.LD.128.USAR.IP q0, %[mem], 16" "\n"
            "EE.VLD.128.XP q1, %[mem], %[skip]" "\n"
            : [mem] "+r" (mem)
            : [skip] "r" (skip)
        );

        std::array<uint32_t,VBLOCK_CNT> results {};

        for(unsigned i = 0; i < VBLOCK_CNT; ++i) {

            uint32_t sum;

            // This is where the magic happens:
            // Loading+accumulating one row's data and moving to the next row - taking only 4 CPU clock cycles per row.
            asm (
                "EE.ZERO.ACCX" "\n" // ACCX := 0

                "LOOPNEZ %[cnt], .Lend_%=" "\n" // Repeat for every row, i.e. BLOCK_HEIGHT times
                    "EE.SRC.Q q2, q0, q1" "\n" // align data from (q1:q0) into q2
                    "EE.LD.128.USAR.IP q0, %[mem], 16" "\n" // start loading next q0 from memory
                    "EE.VMULAS.U8.ACCX.LD.XP q1, %[mem], %[skip], q2, q7" "\n" // accumulate this row's data into ACCX and move to the next row
                ".Lend_%=:" "\n"

                // Get the sum from ACCX:
                "RUR.ACCX_0 %[sum]" "\n"
                : [mem] "+r" (mem),
                [sum] "=r" (sum)
                : [cnt] "r" (BLOCK_HEIGHT), [skip] "r" (skip), "m" (dummy)
            );

            uint32_t d = BLOCK_WIDTH*BLOCK_HEIGHT;
            asm ("":"+r" (d)); // This "persuades" gcc to use hardware division (=faster) below

            results[i] = sum / d;

        }
        return results;
    }
} // namespace detail

static inline uint32_t get_block_avg(const uint8_t* mem, const uint32_t rowlen) {
    return detail::blkavg<1>(mem,rowlen)[0];
}

static inline DoubleVAvg get_double_block_avg(const uint8_t* mem, const uint32_t rowlen) {
    return detail::blkavg<2>(mem,rowlen);
}

static constexpr unsigned COLS = 50;
static constexpr unsigned ROWS = 50;
static uint8_t PIXELS[COLS*ROWS];

extern "C" void testBlockAvg(void) {
    static const char* const TAG = "TEST";

    for(unsigned ys = 0; ys <= ROWS-BLOCK_HEIGHT; ys += BLOCK_HEIGHT) {
        for(unsigned xs = 0; xs <= COLS-BLOCK_WIDTH; xs += BLOCK_WIDTH) {
            uint8_t* const block = PIXELS + xs + (ys * COLS);
            for(unsigned y = 0; y < BLOCK_HEIGHT; ++y) {
                for(unsigned x = 0; x < BLOCK_WIDTH; ++x) {
                    block[x+y*COLS] = x+xs+ys;
                }
            }
        }
    }

    for(uint32_t ys = 0; ys <= ROWS-BLOCK_HEIGHT; ys += BLOCK_HEIGHT) {
        for(uint32_t xs = 0; xs <= COLS-BLOCK_WIDTH; xs += BLOCK_WIDTH) {
            uint8_t* const block = PIXELS + xs + (ys * COLS);
            uint32_t a = get_block_avg(block,COLS);
            ESP_LOGI(TAG, "block %" PRIu32 ":%" PRIu32 " -> %" PRIu32, xs, ys, a);
        }
    }

}




static constexpr const char* TAG = "jobfact";

jobfact::Work wrk {};

void work_release_merkles(void) {
    wrk.release();
}
void bldwrk( mining_notify* mn,
    const char* xn1,
    uint32_t xn2len
) {
    wrk.release();
    wrk.reset();

    wrk.jobId.fromStr(mn->job_id);
    wrk.setPrevBlockHash(mn->prev_block_hash);

    wrk.setMerkleBranches(mn->merkle__branches);
    mn->merkle__branches = nullptr;

    wrk.target = mn->target;
    wrk.version = mn->version;
    wrk.ntime = mn->ntime;
    wrk.appendToCb(mn->coinbase_1);
    // Nonce n {GLOBAL_STATE.extranonce_str};
    // n.fromStr(GLOBAL_STATE.extranonce_str);
    wrk.appendToCb(jobfact::Nonce {xn1});
    // wrk.appendToCb(*xn1);
    wrk.appendXn2Space(xn2len);
    wrk.appendToCb(mn->coinbase_2);

    ESP_LOGW(TAG, "Wrk built.");
    printf("Coinbase: %" PRIu32 " bytes, xn2 @ %" PRIu16 "-%" PRIu16 "\n",(uint32_t)wrk.coinbase.size(), wrk.coinbase.xn2Pos, (uint16_t)(wrk.coinbase.xn2Pos+wrk.coinbase.xn2Len));

}

void setXn2(const char* xn2) {
    hex::hex2bin(xn2,wrk.coinbase.u8 + wrk.coinbase.xn2Pos,wrk.coinbase.xn2Len);
}

void cmpcb(const MemSpan_t cb) {
    const auto d = wrk.coinbase.span();
    if(cb.size == d.size_bytes()) {
        uint32_t i;
        for(i = 0; i < cb.size && (cb.start_u8[i] == d[i]); ++i) {

        }
        if(i >= cb.size) {
            ESP_LOGI(TAG, "CBs match!");
        } else {
            ESP_LOGE(TAG, "CBs don't match @ %" PRIu32, i);
        }
    } else {
        ESP_LOGW(TAG, "CB sizes differ: %" PRIu32 " vs. %" PRIu32, (uint32_t)cb.size, (uint32_t)d.size_bytes());
    }
}