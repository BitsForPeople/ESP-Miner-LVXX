#include "bm1366.h"

extern "C" {
    
#include <pthread.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "crc.h"
#include "global_state.h"
#include "serial.h"
#include "utils.h"

#include "asic_utils.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "frequency_transition_bmXX.h"
#include "pll.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#pragma GCC diagnostic pop
}

#include <array>

#include "crc5.hpp"

#define BM1366_CHIP_ID 0x1366
#define BM1366_CHIP_ID_RESPONSE_LENGTH 11

// #define TYPE_JOB 0x20
// #define TYPE_CMD 0x40

static constexpr uint8_t TYPE_JOB = 0x20;
static constexpr uint8_t TYPE_CMD = 0x40;

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

#define MISC_CONTROL 0x18

extern "C" {
    const AsicDrvr_t BM1366_drvr = {
        .id = BM1366,
        .name = "BM1366",
        .hashes_per_clock = 894, // small core count
        .get_compatibility = BM1366_get_compatibility,
        .init = BM1366_init,
        .process_work = BM1366_process_work,
        .set_max_baud = BM1366_set_max_baud,
        .send_work = BM1366_send_work,
        .set_version_mask = BM1366_set_version_mask,
        .send_frequency = BM1366_send_hash_frequency,
        .get_job_frequency_ms = BM1366_get_job_frequency_ms
    };    
}

typedef struct __attribute__((__packed__))
{
    uint8_t job_id;
    uint8_t num_midstates;
    union {
        uint8_t starting_nonce[4];
        uint32_t starting_nonce_u32;
    };
    union {
        uint8_t nbits[4];
        uint32_t nbits_u32;
    };
    union {
        uint8_t ntime[4];
        uint32_t ntime_u32;
    };
    union {
        uint8_t merkle_root[32];
        uint32_t merkle_root_u32[8];
    };
    union {
        uint8_t prev_block_hash[32];
        uint32_t prev_block_hash_u32[8];
    };
    union {
        uint8_t version[4];
        uint32_t version_u32;
    };
} BM1366_job;

static_assert(sizeof(BM1366_job) == 82);


typedef struct __attribute__((__packed__))
{
    uint16_t preamble;
    uint32_t nonce;
    uint8_t midstate_num;
    uint8_t job_id;
    uint16_t version;
    uint8_t crc;
} bm1366_asic_result_t;



static const char * const TAG = "bm1366";

static task_result result;

static constexpr packet_type_t CMD = packet_type_t::CMD_PACKET;
static constexpr packet_type_t JOB = packet_type_t::JOB_PACKET;

#include <array>

template<size_t N>
struct CmdBase_t {
    static constexpr unsigned DATA_LEN = N;
    static constexpr unsigned PRE_LEN = 2+2;
    static constexpr unsigned CRC_LEN = 1;
    static constexpr unsigned TOTAL_LEN = PRE_LEN + DATA_LEN + CRC_LEN;

    static constexpr uint16_t PREAMBL = 0xaa55;

    struct msg_t {
        uint16_t pre {PREAMBL};
        uint8_t cmd {};
        uint8_t len {1+1+N+1}; // cmd, len, DATA, crc
        std::array<uint8_t,N> data {};
        uint8_t crc {};

        constexpr msg_t() = default;
        constexpr msg_t(const uint8_t cmd) :
            pre {PREAMBL},
            cmd {(uint8_t)((cmd | (TYPE_CMD)) & ~(TYPE_JOB))},
            len {1+1+N+1},
            data {},
            crc {} {

            }

        template<size_t L>
        constexpr msg_t(const uint8_t cmd, const uint8_t (&data)[L]) : msg_t(cmd) {
            constexpr size_t C = L < N ? L : N;
            std::copy(data, data+C, this->data.data());
        }

        constexpr msg_t& updateCrc() {
            crc::Crc5 crc {};
            crc.upd(cmd);
            crc.upd(len);
            crc.upd(data.data(),N);
            this->crc = crc.value();
            return *this;
        }        
    };

    union {
        std::array<uint8_t,TOTAL_LEN> bytes;
        msg_t msg {};
    };

    constexpr CmdBase_t() = default;
    constexpr CmdBase_t(const CmdBase_t&) = default;

    constexpr CmdBase_t(const uint8_t cmd) : msg {cmd} {
    }

    template<size_t L>
    constexpr CmdBase_t(const uint8_t cmd, const uint8_t (&data)[L]) : msg {cmd,data} {

    }

    constexpr CmdBase_t& updateCrc() {
        msg.updateCrc();
        return *this;
    }

    constexpr const std::array<uint8_t,TOTAL_LEN>& build() {
        msg.udapteCrc();
        return this->bytes;
    }

    constexpr uint8_t& operator[](const int i) {
        return msg.data[i];
    }

    constexpr const uint8_t& operator[](const int i) const {
        return msg.data[i];
    }

    constexpr std::array<uint8_t,N>& data() {
        return msg.data;
    }

    constexpr const std::array<uint8_t,N>& data() const {
        return msg.data;
    }

    void send() const {
        SERIAL_send(bytes.data(),TOTAL_LEN,false);
    }

};

template<size_t N>
struct UnicastCmd : public CmdBase_t<1+N> {
    private:
    static constexpr uint8_t makeCmd(const uint8_t cmd) {
        return (cmd | GROUP_SINGLE) & ~(GROUP_ALL);
    }
    public:
    using base_t = CmdBase_t<1+N>;
    constexpr UnicastCmd() = default;
    constexpr UnicastCmd(const UnicastCmd<N>& other) = default;

    constexpr UnicastCmd(const uint8_t cmd) : 
        base_t {makeCmd(cmd)}
    {

    }

    constexpr UnicastCmd(const uint8_t cmd, const uint8_t (&data)[N]) :
        base_t {makeCmd(cmd)}
    {
        this->msg.data[0] = 0;
        setData(data);
    }

    constexpr UnicastCmd(const uint8_t cmd, const uint8_t addr, const uint8_t (&data)[N]) :
        base_t {makeCmd(cmd)}
    {
        setAddr(addr);
        setData(data);
    }

    // constexpr uint8_t& addr() {
    //     return this->msg.data[0];
    // }

    // constexpr const uint8_t& addr() const {
    //     return this->msg.data[0];
    // }

    constexpr UnicastCmd& setAddr(const uint8_t addr) {
        this->msg.data[0] = addr;
        return *this;
    }

    constexpr UnicastCmd& setData(const uint8_t (&data)[N]) {
        std::copy(data,data+N,this->msg.data.data()+1);
        return *this;
    }
};


template<std::size_t N>
static void send(const uint8_t (&data)[N]) {
    SERIAL_send(data, sizeof(data), BM1366_SERIALTX_DEBUG);
}

template<std::size_t N>
static void send(const std::array<uint8_t,N>& data) {
    SERIAL_send(data.data(), N, BM1366_SERIALTX_DEBUG);
}

template<typename...Args>
static inline constexpr std::array<uint8_t,sizeof...(Args)+5> makeCmd(uint8_t cmd, const Args&...args) {
    constexpr unsigned PRE_LEN = 2;
    constexpr unsigned CMD_LEN = 1;
    constexpr unsigned LEN_LEN = 1;
    constexpr unsigned DATA_START = PRE_LEN + CMD_LEN + LEN_LEN;
    constexpr unsigned CRC_LEN = 1;

    constexpr unsigned DATA_LEN = sizeof...(Args);
    constexpr unsigned MSG_LEN = CMD_LEN + LEN_LEN + DATA_LEN + CRC_LEN;
    constexpr unsigned TOTAL_LEN = PRE_LEN + MSG_LEN;

    std::array<uint8_t,TOTAL_LEN> arr;

    arr[0] = 0x55;
    arr[1] = 0xaa;
    arr[2] = (cmd | TYPE_CMD) & ~(TYPE_JOB);;
    arr[3] = MSG_LEN;

    unsigned ix = DATA_START;
    ((arr[ix++] = args), ...);

    crc::Crc5 crc5 {};
    // crc5.upd(cmd);
    // crc5.upd(MSG_LEN);
    crc5.upd( arr.data()+PRE_LEN, MSG_LEN - CRC_LEN );

    arr[TOTAL_LEN-1] = crc5.value(); // crc::crc5_finish(crc);
    return arr;

}

template<std::size_t L>
static inline void sendCmd(uint8_t cmd, const uint8_t (&d)[L]) {
    constexpr unsigned PRE_LEN = 2;
    constexpr unsigned CMD_LEN = 1;
    constexpr unsigned LEN_LEN = 1;
    constexpr unsigned DATA_START = PRE_LEN + CMD_LEN + LEN_LEN;
    constexpr unsigned CRC_LEN = 1;

    constexpr unsigned DATA_LEN = L;
    constexpr unsigned MSG_LEN = CMD_LEN + LEN_LEN + DATA_LEN + CRC_LEN;
    constexpr unsigned TOTAL_LEN = PRE_LEN + MSG_LEN;

    std::array<uint8_t,TOTAL_LEN> arr {};

    cmd = (cmd | TYPE_CMD) & ~(TYPE_JOB);

    arr[0] = 0x55;
    arr[1] = 0xaa;
    arr[2] = cmd;
    arr[3] = MSG_LEN;

    std::copy(d,d+L,arr.data()+DATA_START);

    crc::Crc5 crc5 {};
    // crc5.upd(cmd);
    // crc5.upd(MSG_LEN);
    crc5.upd( arr.data()+PRE_LEN, MSG_LEN - CRC_LEN );

    arr[TOTAL_LEN-1] = crc5.value(); // crc::crc5_finish(crc);
    send(arr);
}



constexpr CmdBase_t<2> getChainInactiveCmd() {
    const uint8_t read_address[2] = {0x00, 0x00};
    CmdBase_t<2> cmd {GROUP_ALL | CMD_INACTIVE,read_address};
    return cmd.updateCrc();
}

constexpr CmdBase_t<2> CHAIN_INACTIVE_CMD = CmdBase_t<2> {GROUP_ALL | CMD_INACTIVE, {0x00, 0x00}}.updateCrc();

// = getChainInactiveCmd();

static void _send_chain_inactive(void)
{

    // const unsigned char read_address[2] = {0x00, 0x00};
    // // send serial data
    // _send_BM1366((TYPE_CMD | GROUP_ALL | CMD_INACTIVE), read_address, 2, BM1366_SERIALTX_DEBUG);
    CHAIN_INACTIVE_CMD.send();
    // const uint8_t data[2] = {0x00, 0x00};
    // sendCmd((GROUP_ALL | CMD_INACTIVE), data);
}

static void _set_chip_address(uint8_t chipAddr)
{
    const uint8_t data[] = {chipAddr, 0x00};
    sendCmd((GROUP_SINGLE | CMD_SETADDRESS), data);
}

void BM1366_set_version_mask(uint32_t version_mask) 
{
    const uint32_t versions_to_roll = version_mask >> 13;
    const uint8_t version_byte0 = (versions_to_roll >> 8);
    const uint8_t version_byte1 = (versions_to_roll & 0xFF); 
    const uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, version_byte0, version_byte1};
    sendCmd( GROUP_ALL | CMD_WRITE, version_cmd );
}

uint32_t BM1366_get_job_frequency_ms(GlobalState* g) {
    return 2000;
}

void BM1366_send_hash_frequency(float target_freq)
{
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float new_freq;
    
    pll_get_parameters(target_freq, 144, 235, &fb_divider, &refdiv, &postdiv1, &postdiv2, &new_freq);
    
    uint8_t vdo_scale = (fb_divider * FREQ_MULT / refdiv >= 2400) ? 0x50 : 0x40;
    uint8_t postdiv = (((postdiv1 - 1) & 0xf) << 4) | ((postdiv2 - 1) & 0xf);
    const uint8_t freqbuf[6] = {0x00, 0x08, vdo_scale, fb_divider, refdiv, postdiv};

    sendCmd((GROUP_ALL | CMD_WRITE), freqbuf );

    // ESP_LOGI(TAG, "Setting Frequency to %g MHz (%g)", target_freq, new_freq);
}

static const uint8_t INIT3[] = {0x55, 0xAA, 0x52, 0x05, 0x00, 0x00, 0x0A};
static const uint8_t INIT4[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA8, 0x00, 0x07, 0x00, 0x00, 0x03};
static const uint8_t INIT5[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x18, 0xFF, 0x0F, 0xC1, 0x00, 0x00};
static const uint8_t INIT135[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x85, 0x40, 0x0C};
static const uint8_t INIT136[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x80, 0x20, 0x19};
static const uint8_t INIT138[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x54, 0x00, 0x00, 0x00, 0x03, 0x1D};
static const uint8_t INIT139[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x58, 0x02, 0x11, 0x11, 0x11, 0x06};
static const uint8_t INIT171[] = {0x55, 0xAA, 0x41, 0x09, 0x00, 0x2C, 0x00, 0x7C, 0x00, 0x03, 0x03};

// Enables version rolling and sets the mask to 0xffff:
static const uint8_t INIT_VER_ROLL[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF, 0x1C};

    // unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x11, 0x5A}; //S19k Pro Default
    // unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x14, 0x46}; //S19XP-Luxos Default
    // orig -> unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x15, 0x1C}; //S19XP-Stock Default
    // unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x0F, 0x00, 0x00}; //supposedly the "full" 32bit nonce range
static const uint8_t SET_10_HASH_COUNTING[] = {0x00, 0x10, 0x00, 0x00, 0x15, 0x1C};



static void sendDiffMask(const uint16_t difficulty) {
    //set difficulty mask
    uint8_t difficulty_mask[6];
    ASIC_get_difficulty_mask(difficulty, difficulty_mask);
    sendCmd((GROUP_ALL | CMD_WRITE), difficulty_mask);
}

unsigned BM1366_get_compatibility(uint16_t chip_id) {
    if(chip_id == 0x1366) {
        return 100;
    } else {
        return 0;
    }
}

uint8_t BM1366_init(float frequency, uint16_t asic_count, uint16_t difficulty)
{
    // ESP_LOGE(TAG, "Yup, we're running!");

    // set version mask
    for (int i = 0; i < 3; i++) {
        BM1366_set_version_mask(STRATUM_DEFAULT_VERSION_MASK);
    }

    // read register 00 on all chips
    // send(INIT3);

    const unsigned chip_counter = asic_count;
    // int chip_counter = count_asic_chips(asic_count, BM1366_CHIP_ID, BM1366_CHIP_ID_RESPONSE_LENGTH);

    // if (chip_counter == 0) {
    //     return 0;
    // }

    send(INIT4);
    send(INIT5);
    _send_chain_inactive();

    // split the chip address space evenly

    const unsigned address_interval = 256u / chip_counter;
    for (unsigned i = 0; i < chip_counter; i++) {
        _set_chip_address(i * address_interval);
    }

    send(INIT135);
    send(INIT136);

    //set difficulty mask
    sendDiffMask(difficulty);

    send(INIT138);
    send(INIT139);
    send(INIT171);

    //S19XP Dump sends baudrate change here.. we wait until later.
    // unsigned char init173[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x28, 0x11, 0x30, 0x02, 0x00, 0x03};
    // _send_simple(init173, 11);

    // UnicastCmd<5> cmd {CMD_WRITE};
    for (unsigned i = 0; i < chip_counter; i++) {
        const uint8_t addr = i * address_interval;
        // cmd.setAddr(addr);
        {
            // cmd.setData({0xA8, 0x00, 0x07, 0x01, 0xF0});
            // cmd.updateCrc().send();

            const uint8_t set_a8_register[6] = {addr, 0xA8, 0x00, 0x07, 0x01, 0xF0};            
            sendCmd((GROUP_SINGLE | CMD_WRITE), set_a8_register);
            // send(makeCmd((GROUP_SINGLE | CMD_WRITE),addr,0xA8, 0x00, 0x07, 0x01, 0xF0));
        }
        {
            // cmd.setData({0x18, 0xF0, 0x00, 0xC1, 0x00}).updateCrc().send();

            const uint8_t set_18_register[6] = {addr, 0x18, 0xF0, 0x00, 0xC1, 0x00};
            sendCmd((GROUP_SINGLE | CMD_WRITE), set_18_register);
            // send(makeCmd((GROUP_SINGLE | CMD_WRITE),addr,0x18, 0xF0, 0x00, 0xC1, 0x00));
        }
        {
            // cmd.setData({0x3C, 0x80, 0x00, 0x85, 0x40}).updateCrc().send();

            const uint8_t set_3c_register_first[6] = {addr, 0x3C, 0x80, 0x00, 0x85, 0x40};
            sendCmd((GROUP_SINGLE | CMD_WRITE), set_3c_register_first);

            // send(makeCmd((GROUP_SINGLE | CMD_WRITE),addr,0x3C, 0x80, 0x00, 0x85, 0x40));
        }
        {
            // cmd.setData({0x3C, 0x80, 0x00, 0x80, 0x20}).updateCrc().send();

            const uint8_t set_3c_register_second[6] = {addr, 0x3C, 0x80, 0x00, 0x80, 0x20};
            sendCmd((GROUP_SINGLE | CMD_WRITE), set_3c_register_second);
            // send(makeCmd((GROUP_SINGLE | CMD_WRITE),addr,0x3C, 0x80, 0x00, 0x80, 0x20));
        }
        {
            // cmd.setData({0x3C, 0x80, 0x00, 0x82, 0xAA}).updateCrc().send();

            const uint8_t set_3c_register_third[6] = {addr, 0x3C, 0x80, 0x00, 0x82, 0xAA};
            sendCmd((GROUP_SINGLE | CMD_WRITE), set_3c_register_third);
            // send(makeCmd((GROUP_SINGLE | CMD_WRITE),addr,0x3C, 0x80, 0x00, 0x82, 0xAA));
        }
    }

    do_frequency_transition(frequency, BM1366_send_hash_frequency);

    //register 10 is still a bit of a mystery. discussion: https://github.com/bitaxeorg/ESP-Miner/pull/167

    sendCmd((GROUP_ALL | CMD_WRITE), SET_10_HASH_COUNTING);

    send(INIT_VER_ROLL);

    return chip_counter;
}

// Baud formula = 25M/((denominator+1)*8)
// The denominator is 5 bits found in the misc_control (bits 9-13)
int BM1366_set_default_baud(void)
{
    // default divider of 26 (11010) for 115,749
    // unsigned char baudrate[9] = {0x00, MISC_CONTROL, 0x00, 0x00, 0b01111010, 0b00110001}; // baudrate - misc_control
    // _send_BM1366((TYPE_CMD | GROUP_ALL | CMD_WRITE), baudrate, 6, BM1366_SERIALTX_DEBUG);
    const uint8_t baudrate[9] = {0x00, MISC_CONTROL, 0x00, 0x00, 0b01111010, 0b00110001}; // baudrate - misc_control
    sendCmd((GROUP_ALL | CMD_WRITE), baudrate);   
    return 115749;
}

static constexpr uint8_t REG28[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x28, 0x11, 0x30, 0x02, 0x00, 0x03};
int BM1366_set_max_baud(void)
{
    ESP_LOGI(TAG, "Setting max baud of 1000000");

    send(REG28);
    return 1000000;
}

static uint8_t id = 0;


template<typename P>
static inline void incp(P*& p, const int inc) {
    p = (P*)((uintptr_t)p + inc);
}

template<typename T, typename S, typename D>
static inline void cpyAs(const S*& src, D*& dst) {
    *(T*)dst = *(const T*)src;
    incp(src,sizeof(T));
    incp(dst,sizeof(T));
}

template<unsigned L>
static void inline cpy(const void* src, void* dst) {
    if constexpr (L >= 8) {
        for(unsigned i = 0; i < L/8; ++i) {
            cpyAs<uint64_t>(src,dst);
        }
    }
    if constexpr (L & 4) {
        cpyAs<uint32_t>(src,dst);
    }
    if constexpr (L&2) {
        cpyAs<uint16_t>(src,dst);
    }
    if constexpr (L&1) {
        cpyAs<uint8_t>(src,dst);
    }
}

template<std::size_t WRDCNT, typename S, typename D>
static void cpyWordsReverse(const S* const src, D* dst) {
    if constexpr (WRDCNT > 0) {
        uint32_t* d = (uint32_t*)dst;
        uint32_t* const end = d + WRDCNT;
        const uint32_t* s = ((const uint32_t*)src) + WRDCNT - 1;
        do {
            *d = *s;
            ++d;
            --s;
        } while(d < end);
    }
}

template<typename T>
static inline T& al(T* x) {
    return *(T*)(__builtin_assume_aligned(x,alignof(T)));
}

static void makeJob(const uint8_t id, const bm_job& next_bm_job, BM1366_job& job) {

    job.job_id = id;
    job.num_midstates = 0x01;
    job.starting_nonce_u32 = next_bm_job.starting_nonce;

    job.nbits_u32 = next_bm_job.target;
    job.ntime_u32 = next_bm_job.ntime;

    cpyWordsReverse<8>(next_bm_job.merkle_root, job.merkle_root);
    cpyWordsReverse<8>(next_bm_job.prev_block_hash, job.prev_block_hash);

    job.version_u32 = next_bm_job.version;
}

static constexpr unsigned JOBMSG_LEN = 2 + 1 + 1 + sizeof(BM1366_job) + 2;

struct JobMsg {
    private:
        static constexpr unsigned DATA_LEN = sizeof(BM1366_job);
        static constexpr unsigned CRC_LEN = 2;
        static constexpr unsigned PRE_LEN = 2;
        static constexpr unsigned CMD_LEN = 1;
        static constexpr unsigned LEN_LEN = 1;
        static constexpr unsigned MSG_LEN = CMD_LEN + LEN_LEN + DATA_LEN + CRC_LEN;

        static constexpr std::array<uint8_t,4> PREFIX {0x55, 0xaa, (TYPE_JOB | GROUP_SINGLE | CMD_WRITE), MSG_LEN};

    public:
    static constexpr unsigned TOTAL_LEN = PRE_LEN + MSG_LEN;
    struct Msg {
        uint16_t pre {};
        uint8_t cmd {};
        uint8_t len {};
        BM1366_job job {};
        uint16_t crc {};

        constexpr void init() {
            pre = 0xaa55;
            cmd = (TYPE_JOB | GROUP_SINGLE | CMD_WRITE);
            len = MSG_LEN;
        }

        constexpr Msg& build() {
            init();
            const uint16_t c = crc16_false(&this->cmd, MSG_LEN - CRC_LEN);
            this->crc = (c >> 8) | (c << 8);
            return *this;
        }
    };

    static_assert(sizeof(Msg) == TOTAL_LEN);

    union {
        std::array<uint8_t,TOTAL_LEN> arr {};
        Msg msg;
    };

    constexpr JobMsg() = default;

    constexpr JobMsg(const uint8_t id, const bm_job& job) {
        (void)arr;
        makeJob(id, job, this->msg.job);
    }

    constexpr const std::array<uint8_t,TOTAL_LEN>& build() {
        msg.build();
        return this->arr;
    }

};

void BM1366_send_work(GlobalState* const GLOBAL_STATE, bm_job* const next_bm_job)
{
    const uint8_t newJobId = (id = (id + 8) % 128);

    JobMsg jm {newJobId, *next_bm_job};

    if (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[newJobId] != NULL) {
        free_bm_job(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[newJobId]);
    }

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[newJobId] = next_bm_job;

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    GLOBAL_STATE->valid_jobs[newJobId] = 1;
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

    //debug sent jobs - this can get crazy if the interval is short
    #if BM1366_DEBUG_JOBS
    ESP_LOGI(TAG, "Send Job: %02X", newJobId);
    #endif

    // _send_BM1366((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t *)&job, sizeof(BM1366_job), BM1366_DEBUG_WORK);
    send(jm.build());
}

task_result * BM1366_process_work(GlobalState* const GLOBAL_STATE)
{
    bm1366_asic_result_t asic_result {};

    if (ASIC_receive_work((uint8_t *)&asic_result, sizeof(asic_result)) != ESP_OK) {
        return NULL;
    }

    uint8_t job_id = asic_result.job_id & 0xf8;
    // uint8_t core_id = (uint8_t)((ntohl(asic_result.nonce) >> 25) & 0x7f); // BM1366 has 112 cores, so it should be coded on 7 bits
    // uint8_t small_core_id = asic_result.job_id & 0x07; // BM1366 has 8 small cores, so it should be coded on 3 bits
    uint32_t version_bits = (ntohs(asic_result.version) << 13); // shift the 16 bit value left 13
    // ESP_LOGI(TAG, "Job ID: %02X, Core: %d/%d, Ver: %08" PRIX32, job_id, core_id, small_core_id, version_bits);

    if (GLOBAL_STATE->valid_jobs[job_id] == 0) {
        ESP_LOGW(TAG, "Invalid job found, 0x%02X", job_id);
        return NULL;
    }

    uint32_t rolled_version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->version | version_bits;

    result.job_id = job_id;
    result.nonce = asic_result.nonce;
    result.rolled_version = rolled_version;

    return &result;
}



