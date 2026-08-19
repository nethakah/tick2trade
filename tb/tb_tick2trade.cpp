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

static void tick(Vtick2trade_top *dut){
    REQUIRES(dut != nullptr);
    //

    sim_time++;
    int dma_phase = sim_time % DMA_PERIOD;
    int core_phase = sim_time % CORE_PERIOD;

    if (dma_phase == 0) dut->dma_clk = 1; // rising
    else if (dma_phase == DMA_PERIOD/2) dut->dma_clk = 0; // falling
    if (core_phase == 0) dut->core_clk = 1; // rising
    else if (core_phase == DMA_PERIOD/2) dut->core_clk = 0; // falling
    // period/2 because 2 edges per period
}

static bool dma_rising(){
    return (sim_time % DMA_PERIOD) == 0;
}

static void reset(Vtick2trade_top *dut){
    REQUIRES(dut != nullptr);
    //

    dut->dma_clk = 0;
    dut->core_clk = 0;
    dut->dma_rst_n = 0;
    dut->core_rst_n = 0;
    dut->s_axis_tdata = 0;
    dut->s_axis_tvalid = 0;
    dut->s_axis_tlast = 0;
    dut->cfg_armed = 0;
    dut->cfg_side = 0;
    dut->cfg_trigger_price = 0;
    dut->cfg_order_shares = 0;
    dut->cfg_spread_max = 0;
    dut->cfg_size_min = 0;
    
    for (int i = 0; i < RESET_UNITS; i++){
        tick(dut);
    }
    dut->dma_rst_n = 1;
    dut->core_rst_n = 1;
    for (int i = 0; i < RESET_UNITS; i++){
        tick(dut);
    }

    //
    ENSURES(dut->best_bid_price == BID_EMPTY);
    ENSURES(dut->best_ask_price == ASK_EMPTY);
}

static Vtick2trade_top *fresh_dut(){
    Vtick2trade_top *dut = new Vtick2trade_top;
    reset(dut);
    return dut;
}

static void preload(
    Vtick2trade_top *dut,
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

static void push_byte(
    Vtick2trade *dut,
    uint8_t b,
    bool last,
    int max_time = DMA_PERIOD*50
){
    REQUIRES(dut != nullptr);
    REQUIRES(dut->dma_rst_n == 1);
    //

    dut->s_axis_tdata = b;
    dut->s_axis_tlast = last ? 1 : 0;
    dut->s_axis_tvalid = 1;

    for (int i = 0; i < max_time; i++){
        tick(dut);
        if (dma_rising() && dut->s_axis_tready){
            tick(dut);
            dut->s_axis_tvalid = 0;
            return;
        }
    }

    std::printf("FAILED: the byte was not accepted by the DMA interface");
    failures++;
}

static void push_packet(
    Vtick2trade_top *dut,
    const uint8_t *packet,
    size_t len,
    int drain_time = CORE_PERIOD*400
){
    REQUIRES(dut != nullptr);
    REQUIRES(packet != nullptr);
    REQUIRES(len > 0);
    //

    for (size_t i = 0; i < len; i++){
        push_byte(dut, packet[i], i==len-1);
    }
    dut->s_axis_tvalid = 0;

    for int i = 0; i < drain_time; i++){
        tick(dut);
    }
}

// reset to baselines works
static void test_reset(){
    std::printf("TEST1: resets to empty book\n");
    Vtick2trade_top *dut = fresh_dut();

    check(dut->best_bid_price, BID_EMPTY, "bid at baseline");
    check(dut->best_ask_price, ASK_EMPTY, "ask at baseline");
    check(dut->packet_count, 0, "packet_count = 0");
    check(dut->gap_count, 0, "gap_count = 0");
    check(dut->fire_count, 0, "fire_count = 0");
    check(dut->miss_count, 0, "miss_count = 0");
    check(dut->overflow_count, 0, "overflow_count = 0");

    dut->final();
    delete dut;
}

static void test_end2end_book(){
    std::printf("TEST2: on raw packet input, order book state outputs accordingly\n")
    Vtick2trade_top *dut = fresh_dut();
}

static void test_end2end_fire(){
    std::printf("TEST3: on raw packet input, order fires accordingly\n");
    Vtick2trade_top *dut = fresh_dut();
}

static void test_gap_detection(){
    std::printf("TEST4: sequence gap detected from end to end of pipeline\n")
    Vtick2trade_top *dut = fresh_dut();
}

static void test_kill_switch(){
    std::printf("TEST5: kill switch blocks fires immediately end to end of pipeline\n")
    Vtick2trade_top *dut = fresh_dut();
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