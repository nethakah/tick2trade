/*
rm -rf obj_dir
verilator --cc --exe --build -j 0  --assert +define+SIM --top-module moldudp_deframer rtl/msg_pkg.sv rtl/moldudp_deframer.sv tb/tb_moldudp_deframer.cpp
./obj_dir/Vmoldudp_deframer
*/

#include "Vmoldudp_deframer.h"
#include <verilated.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "contracts.hpp"
#include "itch_messages.hpp"

static int failures = 0;
static constexpr int RESET_CYCLES = 5;

static void check(uint64_t actual, uint64_t expected, const char *name){
    REQUIRES(name != nullptr);
    //

    if (actual != expected){
        std::printf("FAILED %-24s. got: 0x%llx; expected: 0x%llx\n",
                    name, (unsigned long long)actual, (unsigned long long)expected);
        failures++;
    }
}

static void tick(Vmoldudp_deframer *dut){
    REQUIRES(dut != nullptr);
    //

    dut->clk = 0;
    dut->eval();
    dut->clk = 1;
    dut->eval();
}

static void reset (Vmoldudp_deframer *dut){
    REQUIRES(dut != nullptr);
    //

    dut->rst_n = 0;
    dut->s_axis_tvalid = 0;
    dut->s_axis_tdata = 0;
    dut->s_axis_tlast = 0;
    dut->m_axis_tready = 1;

    for (int i = 0; i < RESET_CYCLES; i++){
        tick(dut);
    }

    dut->rst_n = 1;
    tick(dut);

    //
    ENSURES(dut->rst_n == 1);
    ENSURES(dut->m_axis_tvalid == 0);
}

static void feed_byte(
    Vmoldudp_deframer *dut,
    uint8_t b,
    bool last,
    uint8_t *out,
    size_t *out_len,
    int max_wait = 100
){
    REQUIRES(dut != nullptr);
    REQUIRES(out != nullptr);
    REQUIRES(out_len != nullptr);
    REQUIRES(dut->rst_n == 1);

    dut->s_axis_tdata = b;
    dut->s_axis_tvalid = 1;
    dut->s_axis_tlast = last ? 1 : 0;

    for (int wait = 0; wait < max_wait; wait++){
        dut->eval();
        bool accepted = dut->s_axis_tready;
        if (dut->m_axis_tvalid && dut->m_axis_tready){
            out[*out_len] = dut->m_axis_tdata;
            (*out_len)++;
        }
        
        tick(dut);
        if (accepted){
            return;
        }
    }

    std::printf("FAILED: byte stalled >%d cycles\n", max_wait);
    failures++;
}

static void drain(
    Vmoldudp_deframer *dut,
    uint8_t *out,
    size_t *out_len, 
    int cycles = 10
){
    REQUIRES(dut != nullptr);
    REQUIRES(out != nullptr);
    REQUIRES(out_len != nullptr);
    //

    dut->s_axis_tvalid = 0;

    for (int i = 0; i < cycles; i++){
        dut->eval();
        if (dut->m_axis_tvalid && dut->m_axis_tready){
            out[*out_len] = dut->m_axis_tdata;
            (*out_len)++;
        }
        tick(dut);
    }
}

static void test_single_packet(Vmoldudp_deframer *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TESTING: single packet; 2 msgs\n");
    reset(dut);

    OrderAdd add;
    add.stock_locate = 1;
    add.tracking_num = 1;
    add.timestamp = 1000;
    add.order_ref_num = 100;
    add.is_buy = true;
    add.shares = 500;
    add.stock = "AAPL";
    add.price = 1500000;

    OrderDelete del;
    del.stock_locate = 1;
    del.tracking_num = 2;
    del.timestamp = 2000;
    del.order_ref_num = 100;

    uint8_t itch_a[ORDER_ADD_LEN];
    uint8_t itch_d[ORDER_DELETE_LEN];
    build_order_add(itch_a, add);
    build_order_delete(itch_d, del);

    // 20 header + 2+36 + 2+19 = 79 bytes
    uint8_t packet[128];
    size_t n = 0;
    build_mold_header(&packet[0], 1000, 2); // seq 1000; 2 msgs
    n = MOLD_HEADER_LEN;
    n += build_mold_msg(&packet[n], itch_a, ORDER_ADD_LEN);
    n += build_mold_msg(&packet[n], itch_d, ORDER_DELETE_LEN);

    uint8_t out[128]; // so we have enough room >79
    size_t out_len = 0;

    // feed bytes of packet and asssert tlast on final byte
    for (size_t i = 0; i < n; i++){
        feed_byte(dut, packet[i], i==n-1, out, &out_len);
    }
    drain(dut, out, &out_len);

    // check1: itch bytes came out (not more not less)
    check(out_len, ORDER_ADD_LEN+ORDER_DELETE_LEN, "output length");
    if (out_len != ORDER_ADD_LEN+ORDER_DELETE_LEN){
        return;
    }

    // check2: bytes are identical to built ones
    check(std::memcmp(out, itch_a, ORDER_ADD_LEN) == 0, 1, "msg 0 bytes match");
    check(std::memcmp(out+ORDER_ADD_LEN, itch_d, ORDER_DELETE_LEN) == 0, 1, "msg 1 bytes match");

    // check3: status outputs
    check(dut->sequence_num, 1000, "sequence_num");
    check(dut->packet_count, 1, "packet_count");
    check(dut->gap_count, 0, "gap_count");
}

static void test_gap_detect(Vmoldudp_deframer *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TESTING: sequence gap detection\n");
    reset(dut);

    OrderDelete del;
    del.stock_locate = 1;
    del.tracking_num = 1;
    del.timestamp = 1000;
    del.order_ref_num = 100;

    uint8_t itch_d[ORDER_DELETE_LEN];
    build_order_delete(itch_d, del);

    uint8_t out[256]; // 4 msgs * 19 bytes = 76
    size_t out_len = 0;

    // gap is the jump of 3 past the expected 1002 after 1000
    uint64_t sequences[2] = {1000, 1005};

    for (int p = 0; p < 2; p++){
        uint8_t packet[128];
        size_t n = 0;
        build_mold_header(&packet[0], sequences[p], 2); // 2 msgs/pkt

        n = MOLD_HEADER_LEN;
        n += build_mold_msg(&packet[n], itch_d, ORDER_DELETE_LEN);
        n += build_mold_msg(&packet[n], itch_d, ORDER_DELETE_LEN);

        for (size_t i = 0; i < n; i++){
            feed_byte(dut, packet[i], i==n-1, out, &out_len);
        }
    }

    drain(dut, out, &out_len);
    check(dut->packet_count, 2, "packet_count");
    check(dut->gap_count, 3, "gap_count (1005-1002)"); // notice 3 msgs missing
    check(out_len, ORDER_DELETE_LEN*4, "all 4 received msgs passed"); // the non-missed ones passed cleanly

}

int main(int argc, char **argv){
    Verilated::commandArgs(argc, argv);
    Vmoldudp_deframer *dut = new Vmoldudp_deframer;

    test_single_packet(dut);
    test_gap_detect(dut);

    std::printf("\n%s (Failures: %d)\n",
                failures ? "FAILED" : "PASSED", failures);
    
    dut->final(); // tells Verilator simulation is over
    delete dut; // free()

    return failures ? 1 : 0;
}