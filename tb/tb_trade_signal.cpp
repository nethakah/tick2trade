/*
rm -rf obj_dir
verilator --cc --exe --build -j 0 --assert +define+SIM --top-module trade_signal rtl/msg_pkg.sv rtl/trade_signal.sv tb/tb_trade_signal.cpp
./obj_dir/Vtrade_signal
*/

#include "Vtrade_signal.h"
#include <verilated.h>
#include <cstdio>
#include <cstdint>
#include "contracts.hpp"

static int failures = 0;
static constexpr int RESET_CYCLES = 5;
static constexpr uint32_t ASK_EMPTY = 0xFFFFFFFF;
static constexpr uint32_t BID_EMPTY = 0;

static void tick(Vtrade_signal *dut){
    REQUIRES(dut != nullptr);
    //

    dut->clk = 0;
    dut->eval();
    dut->clk = 1;
    dut->eval();

    //
    ENSURES(dut->clk == 1);
}

static void reset(Vtrade_signal *dut){
    REQUIRES(dut != nullptr);
    //

    dut->rst_n = 0;
    dut->book_valid = 0;
    dut->cfg_armed = 0;
    dut->cfg_side = 0;
    dut->cfg_trigger_price = 0;
    dut->cfg_order_shares = 0;
    dut->cfg_spread_max = 0;
    dut->cfg_size_min = 0;
    dut->best_bid_price = BID_EMPTY;
    dut->best_ask_price = ASK_EMPTY;
    dut->best_bid_shares = 0;
    dut->best_ask_shares = 0;

    for (int i = 0; i < RESET_CYCLES; i++){
        tick(dut);
    }
    
    dut->rst_n = 1;
    tick(dut);

    //
    ENSURES(dut->rst_n == 1)
    ENSURES(dut->order_fire == 0);
}

static void check(uint64_t actual, uint64_t expected, const char *name){
    REQUIRES(name != nullptr);
    //
    
    if (actual != expected){
        std::printf("FAIL %-32s got: 0x%llx, expected: 0x%llx\n",
                    name, (unsigned long long)actual, (unsigned long long)expected);
        failures++;
    }
}

static Vorder_book *fresh_dut(){
    Vorder_book *dut = new Vorder_book;
    reset(dut);

    ENSURES(dut != nullptr);
    return dut;
}

static void preload_order(
    Vtrade_signal *dut,
    bool armed,
    bool side,
    uint32_t trigger_price,
    uint32_t order_shares,
    uint32_t spread_max,
    uint32_t size_min
){
    REQUIRES(dut != nullptr);
    //

    dut->cfg_armed = armed ? 1 : 0;
    dut->cfg_side = side ? 1 : 0;
    dut->cfg_trigger_price = trigger_price;
    dut->cfg_order_shares = order_shares;
    dut->cfg_spread_max = spread_max;
    dut->cfg_size_min = size_min;
}

