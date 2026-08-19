/*
rm -rf obj_dir
verilator --cc --exe --build -j 0 --assert +define+SIM --top-module tick2trade_top rtl/msg_pkg.sv rtl/skid_buffer.sv rtl/async_fifo.sv rtl/moldudp_deframer.sv rtl/itch_parser.sv rtl/order_book.sv rtl/trade_signal.sv rtl/tick2trade_top.sv tb/tb_tick2trade.cpp
./obj_dir/Vtick2trade_top
*/

#include "Vtick2trade_top.h"
#include <verilated.h>
#include <cstdio>
#include <cstdint>
#include "contracts.hpp"
#include "itch_messages.hpp"

static int failures = 0;
static constexpr int DMA_PERIOD = 20;
static constexpr int CORE_PERIOD = 13;
static constexpr int RESET_UNITS = DMA_PERIOD * 5; // scale off slower clk
static constexpr uint32_t BID_EMPTY = 0;
static constexpr uint32_t ASK_EMPTY = 0xFFFFFFFF;

static uint64_t sim_time = 0;

static void check(uint64_t actual, uint64_t expected, const char *name){
    REQUIRES(name != nullptr);
    //
    
    if (actual != expected){
        std::printf("FAIL %-34s got: 0x%llx, expected: 0x%llx\n",
                    name, (unsigned long long)actual, (unsigned long long)expected);
        failures++;
    }
}

int main(int argc, char **argv){
    Verilated::commandArgs(argc, argv);

    test_reset();
    test_end2end_book();
    test_end2end_fire();
    test_gap_detection();
    test_kill_switch();

    std::printf("\n%s (Failures: %d)\n",
                failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}