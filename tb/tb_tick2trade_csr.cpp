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
static constexpr uint32_t REG_FIRE_LATENCY = 0x4C;
static constexpr uint32_t REG_LATENCY_MIN = 0x50;
static constexpr uint32_t REG_LATENCY_MAX = 0x54;

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
    dut->miss_count = 0;
    dut->overflow_count = 0;
    dut->level_collision_count = 0;
    dut->gap_count = 0;
    dut->fire_latency_cycles = 0;
    dut->fire_latency_min = 0xFFFFFFFF;
    dut->fire_latency_max = 0;

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
        if (dut->s_axi_bvalid){
            b_done = true;
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

    check(dut->cfg_armed, 0, "cfg_armed low at reset");
    check(dut->cfg_side, 0, "cfg_side");
    check(dut->cfg_trigger_price, 0, "cfg_trigger_price");
    check(dut->cfg_order_shares, 0, "cfg_order_shares");
    check(dut->cfg_stock_locate, 0, "cfg_stock_locate");

    //
    dut->final();
    delete dut;
}

// make sure write lands on corresponding cfg_* port
static void test_write_drives_cfg(){
    std::printf("TEST2: writes should drive cfg_* outputs\n");
    Vtick2trade_csr *dut = fresh_dut();

    axi_write(dut, REG_TRIGGER_PRICE, 1230500);
    check(dut->cfg_trigger_price, 1230500, "cfg_trigger_price");
    axi_write(dut, REG_ORDER_SHARES, 100);
    check(dut->cfg_order_shares, 100, "cfg_order_shares");
    axi_write(dut, REG_STOCK_LOCATE, 0x98761234);
    check(dut->cfg_stock_locate, 0x1234, "cfg_stock_locate cut to 16b");
    axi_write(dut, REG_SPREAD_MAX, 1000);
    check(dut->cfg_spread_max, 1000, "cfg_spread_max");
    axi_write(dut, REG_SIZE_MIN, 50);
    check(dut->cfg_size_min, 50, "cfg_size_min");

    //
    dut->final();
    delete dut;
}

// check ctrl's 2 flags work - bit 0 is armed and bit 1 is the side
static void test_ctrl_bits(){
    std::printf("TEST3: ctrl register flags are packed together properly\n");
    Vtick2trade_csr *dut = fresh_dut();

    axi_write(dut, REG_CONTROL, 0x1);
    check(dut->cfg_side, 0, "sell");
    check(dut->cfg_armed, 1, "armed");
    axi_write(dut, REG_CONTROL, 0x3);
    check(dut->cfg_side, 1, "buy");
    check(dut->cfg_armed, 1, "armed");
    axi_write(dut, REG_CONTROL, 0x0);
    check(dut->cfg_armed, 0, "unarmed");

    //
    dut->final();
    delete dut;
}

static void test_readback(){
    std::printf("TEST4: cfg regs read back fine\n");
    Vtick2trade_csr *dut = fresh_dut();

    axi_write(dut, REG_TRIGGER_PRICE, 1230500);
    check(axi_read(dut, REG_TRIGGER_PRICE), 1230500, "read back trigger_price");
    axi_write(dut, REG_ORDER_SHARES, 100);
    check(axi_read(dut, REG_ORDER_SHARES), 100, "read back of order_shares");
    axi_write(dut, REG_CONTROL, 0x3);
    check(axi_read(dut, REG_CONTROL), 0x3, "read back control flags");

    //
    dut->final();
    delete dut;
}

// status inputs must appear at their respective addresses
static void test_status_read(){
    std::printf("TEST5: status regs reflect actual pipeline inputs\n");
    Vtick2trade_csr *dut = fresh_dut();

    dut->best_bid_price = 1230000;
    dut->best_bid_shares = 500;
    dut->best_ask_price = 1230500;
    dut->best_ask_shares = 300;
    dut->spread = 500;
    dut->fire_count = 7;
    dut->packet_count = 40;
    dut->gap_count = 6;
    dut->miss_count = 2;
    dut->overflow_count = 0;
    dut->level_collision_count = 0;
    dut->fire_latency_cycles = 42;
    dut->fire_latency_min = 42;
    dut->fire_latency_max = 47;

    dut->eval();

    check(axi_read(dut, REG_FIRE_LATENCY), 42, "fire_latency_cycles");
    check(axi_read(dut, REG_LATENCY_MIN), 42, "latency_min");
    check(axi_read(dut, REG_LATENCY_MAX), 47, "latency_max");
    check(axi_read(dut, REG_BEST_BID_PRICE), 1230000, "best_bid_price");
    check(axi_read(dut, REG_BEST_BID_SHARES), 500, "best_bid_shares");
    check(axi_read(dut, REG_BEST_ASK_PRICE), 1230500, "best_ask_price");
    check(axi_read(dut, REG_BEST_ASK_SHARES), 300, "best_ask_shares");
    check(axi_read(dut, REG_SPREAD), 500, "spread");
    check(axi_read(dut, REG_FIRE_COUNT), 7, "fire_count");
    check(axi_read(dut, REG_PACKET_COUNT), 40, "packet_count");
    check(axi_read(dut, REG_GAP_COUNT), 6, "gap_count");
    check(axi_read(dut, REG_MISS_COUNT), 2, "miss_count");

    //
    dut->final();
    delete dut;
}

// writing a RO addr must be a not allowed (silently) and reading an unmapped should return 0
static void test_unmapped_addr(){
    std::printf("TEST6: unmapped and RO addresses work as planned\n");
    Vtick2trade_csr *dut = fresh_dut();

    dut->fire_count = 3;
    dut->eval();

    axi_write(dut, REG_FIRE_COUNT, 0xFFFFFFFF);
    check(axi_read(dut, REG_FIRE_COUNT), 3, "RO reg unchanged by the write");
    check(axi_read(dut, 0xFC), 0, "unmapped address reads 0");
    check(dut->s_axi_rresp, 0, "unmapped read still has OKAY");
    axi_write(dut, REG_TRIGGER_PRICE, 789);
    axi_write(dut, 0xFC, 0x12345678);
    check(dut->cfg_trigger_price, 789, "unmapped write didn't corrupt things");

    //
    dut->final();
    delete dut;
}

// AW and W are independent so we commit once both are latched (doesn't matter which is first)
static void test_write_split_channels(){
    std::printf("TEST7: AW/W independence\n");
    Vtick2trade_csr *dut = fresh_dut();

    dut->s_axi_awaddr = REG_TRIGGER_PRICE;
    dut->s_axi_awvalid = 1;
    dut->s_axi_bready = 1;
    dut->eval();

    check(dut->s_axi_awready, 1, "slave accepts addr");
    tick(dut);
    dut->s_axi_awvalid = 0;

    dut->eval();
    check(dut->s_axi_awready, 0, "refuse 2nd addr while holding one");
    for (int i = 0; i < 5; i++){
        tick(dut);
    }

    check(dut->cfg_trigger_price, 0, "no commit if we only have address");

    dut->s_axi_wdata = 123456;
    dut->s_axi_wstrb = 0xF; // write all bytes
    dut->s_axi_wvalid = 1;

    dut->eval();
    tick(dut);
    dut->s_axi_wvalid = 0;
    for (int i = 0; i < 5; i++){
        tick(dut);
    }

    check(dut->cfg_trigger_price, 123456, "commit once both data and addr arrived");
    dut->s_axi_bready = 0;

    //
    dut->final();
    delete(dut);
}

// make sure we handle receiving data first or address first
static void test_write_data_first(){
    std::printf("TEST8: W arrive before AW\n");
    Vtick2trade_csr *dut = fresh_dut();

    dut->s_axi_wdata = 654321;
    dut->s_axi_wstrb = 0xF;
    dut->s_axi_wvalid = 1;
    dut->s_axi_bready = 1;

    dut->eval();
    check(dut->s_axi_wready, 1, "slave accepts data");
    tick(dut);
    dut->s_axi_wvalid = 0;
    
    dut->eval();
    check(dut->s_axi_wready, 0, "refuse more data when holding some already");

    for (int i = 0; i < 5; i++){
        tick(dut);
    }
    check(dut->cfg_trigger_price, 0, "no committing if we only have data");

    dut->s_axi_awaddr = REG_TRIGGER_PRICE;
    dut->s_axi_awvalid = 1;
    dut->eval();
    tick(dut);
    dut->s_axi_awvalid = 0;

    for (int i = 0; i < 5; i++){
        tick(dut);
    }
    check(dut->cfg_trigger_price, 654321, "commit when addr catches up too");

    dut->s_axi_bready = 0;
    dut->final();
    delete dut;
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
    test_write_data_first();

    std::printf("\n%s (Failures: %d)\n",
                failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}