/*
rm -rf obj_dir
verilator --cc --exe --build -j 0 --assert +define+SIM --top-module tick2trade_top rtl/msg_pkg.sv rtl/skid_buffer.sv rtl/async_fifo.sv rtl/moldudp_deframer.sv rtl/itch_parser.sv rtl/order_book.sv rtl/trade_signal.sv rtl/tick2trade_csr.sv rtl/tick2trade_top.sv tb/tb_tick2trade_top.cpp
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
static constexpr uint32_t REG_CONTROL = 0x00;
static constexpr uint32_t REG_TRIGGER_PRICE = 0x04;
static constexpr uint32_t REG_ORDER_SHARES = 0x08;
static constexpr uint32_t REG_SPREAD_MAX = 0x0C;
static constexpr uint32_t REG_SIZE_MIN = 0x10;
static constexpr uint32_t REG_STOCK_LOCATE = 0x14;
static constexpr uint32_t REG_FIRE_LATENCY = 0x4C;
static constexpr uint32_t REG_LATENCY_MIN = 0x50;
static constexpr uint32_t REG_LATENCY_MAX = 0x54;

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
    else if (core_phase == CORE_PERIOD/2) dut->core_clk = 0; // falling
    // period/2 because 2 edges per period

    dut->eval();
}

static bool dma_rising(){
    return (sim_time % DMA_PERIOD) == 0;
}
static bool core_rising(){
    return (sim_time % CORE_PERIOD) == 0;
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
    
    dut->s_axi_awvalid = 0;
    dut->s_axi_wvalid = 0;
    dut->s_axi_bready = 0;
    dut->s_axi_arvalid = 0;
    dut->s_axi_rready = 0;
    dut->s_axi_wstrb = 0xF;
    
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

static void axi_write(
    Vtick2trade_top *dut,
    uint32_t addr,
    uint32_t data,
    int max_time = CORE_PERIOD*100
){
    REQUIRES(dut != nullptr);
    REQUIRES(dut->core_rst_n==1);
    //

    dut->s_axi_awaddr = addr & 0xFF;
    dut->s_axi_awvalid = 1;
    dut->s_axi_wdata = data;
    dut->s_axi_wstrb = 0xF;
    dut->s_axi_wvalid = 1;
    dut->s_axi_bready = 1;

    bool aw_done = false;
    bool w_done = false;
    bool b_done = false;

    for (int i = 0; i < max_time; i++){
        if (core_rising()){
            if (!aw_done && dut->s_axi_awready){
                aw_done = true;
            }
            if (!w_done && dut->s_axi_wready){
                w_done = true;
            }
            if (dut->s_axi_bvalid){
                b_done = true;
            }
        }
        tick(dut);

        if (aw_done) dut->s_axi_awvalid = 0;
        if (w_done) dut->s_axi_wvalid = 0;
        if (b_done) break;
    }
    dut->s_axi_awvalid = 0;
    dut->s_axi_wvalid = 0;

    for (int i = 0; i < CORE_PERIOD*2; i++){
        tick(dut);
    }
    dut->s_axi_bready = 0;
    
    if (!b_done){
        std::printf("FAILED: tried to write to CSR (0x%02x) but never got a response\n", addr);
        failures++;
    }
}

static uint32_t axi_read(
    Vtick2trade_top *dut,
    uint32_t addr,
    int max_time = CORE_PERIOD*100
){
    REQUIRES(dut != nullptr);
    REQUIRES(dut->core_rst_n == 1);
    //

    dut->s_axi_araddr = addr & 0xFF;
    dut->s_axi_arvalid = 1;
    dut->s_axi_rready = 1;
    uint32_t sample = 0;
    bool ar_done = false;
    bool r_done = false;

    for (int i = 0; i < max_time; i++){
        if (core_rising()){
            if (!ar_done && dut->s_axi_arready){
                ar_done = true;
            }
            if (dut->s_axi_rvalid){
                sample = dut->s_axi_rdata;
                r_done = true;
            }
        }

        tick(dut);
        if (ar_done) dut->s_axi_arvalid = 0;
        if (r_done) break;
    }

    dut->s_axi_arvalid = 0;
    for (int i = 0; i < CORE_PERIOD*2; i++){
        tick(dut);
    }
    dut->s_axi_rready = 0;

    if (!r_done){
        std::printf("FAILED: tried to read from CSR (0x%02x) but it never returned data\n", addr);
        failures++;
    }
    
    return sample;
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

    axi_write(dut, REG_STOCK_LOCATE, 1);
    axi_write(dut, REG_TRIGGER_PRICE, trigger_price);
    axi_write(dut, REG_ORDER_SHARES, order_shares);
    axi_write(dut, REG_SPREAD_MAX, spread_max);
    axi_write(dut, REG_SIZE_MIN, size_min);
    axi_write(dut, REG_CONTROL, (armed? 0x1 : 0x0) | (side? 0x2 : 0x0));
}

static void push_byte(
    Vtick2trade_top *dut,
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

    for (int i = 0; i < drain_time; i++){
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

// check MoldUDP64 packet with 2 Add orders goes in and book shows both sides
static void test_end2end_book(){
    std::printf("TEST2: on raw packet input, order book state outputs accordingly\n");
    Vtick2trade_top *dut = fresh_dut();
    axi_write(dut, REG_STOCK_LOCATE, 1);

    OrderAdd bid;
    bid.stock_locate  = 1;
    bid.tracking_num  = 1;
    bid.timestamp = 1000;
    bid.order_ref_num = 100;
    bid.is_buy = true;
    bid.shares = 500;
    bid.stock = "AAPL";
    bid.price = 1230000;

    OrderAdd ask;
    ask.stock_locate = 1;
    ask.tracking_num = 2;
    ask.timestamp = 2000;
    ask.order_ref_num = 200;
    ask.is_buy = false;
    ask.shares = 300;
    ask.stock = "AAPL";
    ask.price = 1230500;

    uint8_t itch_bid[ORDER_ADD_LEN];
    uint8_t itch_ask[ORDER_ADD_LEN];
    build_order_add(itch_bid, bid);
    build_order_add(itch_ask, ask);

    uint8_t packet[128];
    size_t n = 0;
    build_mold_header(&packet[0], 1000, 2);
    n = MOLD_HEADER_LEN;
    n += build_mold_msg(&packet[n], itch_bid, ORDER_ADD_LEN);
    n += build_mold_msg(&packet[n], itch_ask, ORDER_ADD_LEN);

    push_packet(dut, packet, n);
    check(dut->best_bid_price, 1230000, "best bid from raw bytes");
    check(dut->best_ask_price, 1230500, "best ask from raw bytes");
    check(dut->best_bid_shares, 500, "best bid shares");
    check(dut->best_ask_shares, 300, "best ask shares");
    check(dut->packet_count, 1, "packet_count = 1");
    check(dut->gap_count, 0, "no gaps");
    check(dut->sequence_num, 1000, "seq num");
    check(dut->spread, 500, "spread computed");
    check(dut->miss_count, 0, "no misses");
    check(dut->overflow_count, 0, "no overflow");
    check(dut->level_collision_count, 0, "no L2 level collisions");

    dut->final();
    delete dut;
}

// check raw packets in one side and fired order on other side
static void test_end2end_fire(){
    std::printf("TEST3: on raw packet input, order fires accordingly\n");
    Vtick2trade_top *dut = fresh_dut();

    preload(dut, true, true, 1230500, 100, 1000, 50);
    // buy 100 shares if ask is 123.05 or better

    OrderAdd bid;
    bid.stock_locate  = 1;
    bid.tracking_num  = 1;
    bid.timestamp = 1000;
    bid.order_ref_num = 100;
    bid.is_buy = true;
    bid.shares = 500;
    bid.stock = "AAPL";
    bid.price = 1230000;

    OrderAdd ask;
    ask.stock_locate = 1;
    ask.tracking_num = 2;
    ask.timestamp = 2000;
    ask.order_ref_num = 200;
    ask.is_buy = false;
    ask.shares = 300;
    ask.stock = "AAPL";
    ask.price = 1230500;

    uint8_t itch_bid[ORDER_ADD_LEN];
    uint8_t itch_ask[ORDER_ADD_LEN];
    build_order_add(itch_bid, bid);
    build_order_add(itch_ask, ask);

    uint8_t packet[128];
    size_t n = 0;
    build_mold_header(&packet[0], 1000, 2);
    n = MOLD_HEADER_LEN;
    n += build_mold_msg(&packet[n], itch_bid, ORDER_ADD_LEN);
    n += build_mold_msg(&packet[n], itch_ask, ORDER_ADD_LEN);

    push_packet(dut, packet, n);
    check(dut->fire_count, 1, "1 fire");
    check(dut->order_side, 1, "buy side");
    check(dut->order_price, 1230500, "order has our limit");
    check(dut->order_shares, 100, "order has preloaded size");

    uint32_t cycles = axi_read(dut, REG_FIRE_LATENCY);
    std::printf("\ntick-to-signal: %u cycles (%.1f ns at 3.476ns/cycle)\n",
                (unsigned)cycles, cycles*3.476);

    dut->final();
    delete dut;
}

// push 2 packets w sequence jumps
static void test_gap_detection(){
    std::printf("TEST4: sequence gap detected from end to end of pipeline\n");
    Vtick2trade_top *dut = fresh_dut();
    axi_write(dut, REG_STOCK_LOCATE, 1);

    OrderDelete del;
    del.stock_locate = 1;
    del.tracking_num = 1;
    del.timestamp = 1000;
    del.order_ref_num = 987;

    uint8_t itch_del[ORDER_DELETE_LEN];
    build_order_delete(itch_del, del);

    uint64_t seq[2] = {1000, 1005};
    for (int i = 0; i < 2; i++){
        uint8_t packet[128];
        size_t n = 0;
        build_mold_header(&packet[0], seq[i], 2);
        n = MOLD_HEADER_LEN;
        n += build_mold_msg(&packet[n], itch_del, ORDER_DELETE_LEN);
        n += build_mold_msg(&packet[n], itch_del, ORDER_DELETE_LEN);
        push_packet(dut, packet, n);
    }

    check(dut->packet_count, 2, "2 packets");
    check(dut->gap_count, 3, "1005 - 1002 = 3 messages lost");
    check(dut->miss_count, 4, "4 misses on unknown orders");

    dut->final();
    delete dut;
}

static void test_kill_switch(){
    std::printf("TEST5: kill switch blocks fires immediately end to end of pipeline\n");
    Vtick2trade_top *dut = fresh_dut();

    preload(dut, false, true, 1230500, 100, 1000, 50); // disarmed

    OrderAdd bid;
    bid.stock_locate  = 1;
    bid.tracking_num  = 1;
    bid.timestamp = 1000;
    bid.order_ref_num = 100;
    bid.is_buy = true;
    bid.shares = 500;
    bid.stock = "AAPL";
    bid.price = 1230000;

    OrderAdd ask;
    ask.stock_locate = 1;
    ask.tracking_num = 2;
    ask.timestamp = 2000;
    ask.order_ref_num = 200;
    ask.is_buy = false;
    ask.shares = 300;
    ask.stock = "AAPL";
    ask.price = 1230500;

    uint8_t itch_bid[ORDER_ADD_LEN];
    uint8_t itch_ask[ORDER_ADD_LEN];
    build_order_add(itch_bid, bid);
    build_order_add(itch_ask, ask);

    uint8_t packet[128];
    size_t n = 0;
    build_mold_header(&packet[0], 1000, 2);
    n = MOLD_HEADER_LEN;
    n += build_mold_msg(&packet[n], itch_bid, ORDER_ADD_LEN);
    n += build_mold_msg(&packet[n], itch_ask, ORDER_ADD_LEN);

    push_packet(dut, packet, n);
    check(dut->best_ask_price, 1230500, "book updated still");
    check(dut->fire_count, 0, "nothing fired");

    dut->final();
    delete dut;
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