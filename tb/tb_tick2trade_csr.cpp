/*
rm -rf obj_dir
verilator --cc --exe --build -j 0 --assert +define+SIM --top-module tick2trade_csr rtl/tick2trade_csr.sv tb/tb_tick2trade_csr.cpp
./obj_dir/Vtick2trade_csr
*/

#include "Vtick2trade_csr.h"
#include <verilated.h>
#include <cstdio>
#include <cstdint>
#include "contracts.hpp"

static int failures = 0;
static constexpr int RESET_CYCLES = 5;

// reg byte addresses
static constexpr uint32_t REG_CONTROL = 0x00;
static constexpr uint32_t REG_TRIGGER_PRICE = 0x04;
static constexpr uint32_t REG_ORDER_SHARES = 0x08;
static constexpr uint32_t REG_SPREAD_MAX = 0x0C;
static constexpr uint32_t REG_SIZE_MIN = 0x10;
static constexpr uint32_t REG_STOCK_LOCATE = 0x14;
static constexpr uint32_t REG_BEST_BID_PRICE = 0x20;
static constexpr uint32_t REG_BEST_BID_SHARES = 0x24;
static constexpr uint32_t REG_BEST_ASK_PRICE = 0x28;
static constexpr uint32_t REG_BEST_ASK_SHARES = 0x2C;
static constexpr uint32_t REG_SPREAD = 0x30;
static constexpr uint32_t REG_FIRE_COUNT = 0x34;
static constexpr uint32_t REG_PACKET_COUNT = 0x38;
static constexpr uint32_t REG_GAP_COUNT = 0x3C;
static constexpr uint32_t REG_MISS_COUNT = 0x40;
static constexpr uint32_t REG_OVERFLOW_COUNT = 0x44;
static constexpr uint32_t REG_LEVEL_COLLISION = 0x48;

static void tick(Vtick2trade_csr *dut){
    REQUIRES(dut != nullptr);
    //
    dut->clk = 0;
    dut->eval();
    dut->clk = 1;
    dut->eval();
    //
    ENSURES(dut->clk == 1);
}

static void reset(Vtick2trade_csr *dut){
    REQUIRES(dut != nullptr);
    //
    dut->rst_n = 0;
    dut->s_axi_wvalid = 0;
    dut->s_axi_awvalid = 0;
    dut->s_axi_bready = 0;
    dut->s_axi_arvalid = 0;
    dut->s_axi_rready = 0;
    dut->s_axi_wstrb = 0xF;
    dut->best_bid_price = 0;
    dut->best_ask_price = 0;
    dut->best_bid_shares = 0;
    dut->best_ask_shares = 0;
    dut->spread = 0;
    dut->fire_count = 0;
    dut->packet_count = 0;
    dit->miss_count = 0;
    dut->overflow_count = 0;
    dut->level_collision_count = 0;
    dut->gap_count = 0;

    for (int i = 0; i < RESET_CYCLES; i++){
        tick(dut);
    }
    dut->rst_n = 1;
    tick(dut);

    //
    ENSURES(dut->rst_n == 1);
}

static Vtick2trade_csr *fresh_dut(){
    Vtick2trade_csr *dut = new Vtick2trade_csr;
    reset(dut);

    ENSURES(dut != nullptr);
    return dut;
}

static void check(uint64_t actual, uint64_t expected, const char *name){
    REQUIRES(name != nullptr);
    //
    
    if (actual != expected){
        std::printf("FAIL %-30s got: 0x%llx, expected: 0x%llx\n",
                    name, (unsigned long long)actual, (unsigned long long)expected);
        failures++;
    }
}

static void axi_write(
    Vtick2trade_csr *dut,
    uint32_t addr,
    uint32_t data,
    int max_wait = 100
){
    REQUIRES(dut != nullptr);
    REQUIRES(dut->rst_n == 1);
    //

    dut->s_axi_awvalid = 1;
    dut->s_axi_awaddr = addr & 0xFF;
    dut->s_axi_wdata = data;
    dut->s_axi_wstrb = 0xF;
    dut->s_axi_wvalid = 1;
    dut->s_axi_bready = 1;

    bool aw_done = false;
    bool w_done = false;
    bool b_done = false;

    for (int i = 0; i < max_wait; i++){
        dut->eval();
        if (!w_done && dut->s_axi_wready){
            w_done = true;
        }
        if (!aw_done && dut->s_axi_awready){
            aw_done = true;
        }
        tick(dut);

        if (w_done) dut->s_axi_wvalid = 0;
        if (aw_done) dut->s_axi_awvalid = 0;
        if (b_done) break;
    }
    dut->s_axi_wvalid = 0;
    dut->s_axi_awvalid = 0;
    tick(dut); // to let bvalid clear
    dut->s_axi_bready = 0;

    if (!b_done){
        std::printf("FAILED: tried to write to 0x%02x but never got a response\n", addr);
        failures++;
    }
}

static uint32_t axi_read(
    Vtick2trade_csr *dut,
    uint32_t addr,
    int max_wait = 100
){
    REQUIRES(dut != nullptr);
    REQUIRES(dut->rst_n == 1);
    //

    dut->s_axi_arvalid = 1;
    dut->s_axi_araddr = addr & 0xFF;
    dut->s_axi_rready = 1;

    uint32_t sample = 0;
    bool ar_done = false;
    bool r_done = false;

    for (int i = 0; i < max_wait; i++){
        dut->eval();
        if (!ar_done && dut->s_axi_arready){
            ar_done = true;
        }
        if (dut->s_axi_rvalid){
            sample = dut->s_axi_rdata;
            r_done = true;
        }
        tick(dut);

        if (ar_done) dut->s_axi_arvalid = 0;
        if (r_done) break;
    }
    dut->s_axi_arvalid = 0;
    tick(dut);
    dut->s_axi_rready = 0;

    if (!r_done){
        std::printf("FAILED: tried to read from 0x%02x but it never returned data\n", addr);
        failures++;
    }
    return sample;
}

// cfg should reset to a safe state
static void test_reset_safe(){
    std::printf("TEST1: config should reset disarmed\n");
    Vtick2trade_csr *dut = fresh_dut();
}

static void test_write_drives_cfg(){
    std::printf("TEST1: config should reset disarmed\n");
    Vtick2trade_csr *dut = fresh_dut();
}

static void test_ctrl_bits(){
    std::printf("TEST1: config should reset disarmed\n");
    Vtick2trade_csr *dut = fresh_dut();
}

static void test_readback(){
    std::printf("TEST1: config should reset disarmed\n");
    Vtick2trade_csr *dut = fresh_dut();
}

static void test_status_read(){
    std::printf("TEST1: config should reset disarmed\n");
    Vtick2trade_csr *dut = fresh_dut();
}

static void test_unmapped_addr(){
    std::printf("TEST1: config should reset disarmed\n");
    Vtick2trade_csr *dut = fresh_dut();
}

static void test_write_split_channels(){
    std::printf("TEST1: config should reset disarmed\n");
    Vtick2trade_csr *dut = fresh_dut();
}

int main(int argc, char **argv){
    Verilated::commandArgs(argc, argv);

    test_reset_safe();
    test_write_drives_cfg();
    test_ctrl_bits();
    test_readback();
    test_status_read();
    test_unmapped_addr();
    test_write_split_channels();

    std::printf("\n%s (Failures: %d)\n",
                failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}