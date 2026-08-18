/*
rm -rf obj_dir
verilator --cc --exe --build -j 0 --top-module order_book rtl/msg_pkg.sv rtl/order_book.sv tb/orderbook/tb_orderbook.cpp
./obj_dir/Vorder_book
*/

#include "Vorder_book.h"
#include <verilated.h>
#include <cstdio>
#include <cstdint>
#include "../include/contracts.hpp"

static int failures = 0;
static constexpr int RESET_CYCLES = 5;
static constexpr int TDATA_WORDS = 12; // 384b = 32b * 12 words

// match msg_pkg
static constexpr uint8_t MSG_NONE = 0;
static constexpr uint8_t MSG_ADD = 1;
static constexpr uint8_t MSG_EXC = 2;
static constexpr uint8_t MSG_DEL = 3;

static constexpr uint32_t ASK_EMPTY = 0xFFFFFFFF;
static constexpr uint32_t BID_EMPTY = 0;

static void tick(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    dut->clk = 0;
    dut->eval();
    dut->clk = 1;
    dut->eval();
    
    //
    ENSURES(dut->clk == 1);
}

static void reset(Vorder_book *dut)
{
    REQUIRES(dut != nullptr);
    //
    
    dut->rst_n = 0;
    dut->s_axis_tvalid = 0;

    for (int i = 0; i < TDATA_WORDS; i++){
        dut->s_axis_tdata[i] = 0;
    }

    for (int i = 0; i < RESET_CYCLES; i++){
        tick(dut);
    }

    dut->rst_n = 1;
    tick(dut);

    //
    ENSURES(dut->rst_n == 1);
    ENSURES(dut->s_axis_tvalid == 0);
}


static void check(uint64_t actual, uint64_t expected, const char* name){
    REQUIRES(name != nullptr);
    //
    
    if (actual != expected){
        std::printf("FAIL %-28s got: 0x%llx, expected: 0x%llx\n",
                    name, (unsigned long long)actual, (unsigned long long)expected);
        failures++;
    }
}

// mirrors msg_pkg.sv
static void setup_msg(
    Vorder_book *dut,
    uint8_t msg_type,
    bool is_buy,
    uint16_t stock_locate,
    uint64_t order_ref,
    uint32_t shares,
    uint32_t price
){
    REQUIRES(dut != nullptr);
    //

    for (int i = 0; i < TDATA_WORDS; i++){
        dut->s_axis_tdata[i] = 0;
    }

    dut->s_axis_tdata[2] = price;
    dut->s_axis_tdata[5] = shares;
    dut->s_axis_tdata[6] = (uint32_t)(order_ref & 0xFFFFFFFF);
    dut->s_axis_tdata[7] = (uint32_t)(order_ref>>32);
    dut->s_axis_tdata[10] = (uint32_t)stock_locate;

    if (is_buy){
        dut->s_axis_tdata[11] = (uint32_t)msg_type | ((uint32_t)1 <<4);
    }
    else{
        dut->s_axis_tdata[11] = (uint32_t)msg_type | ((uint32_t)0 <<4);
    }
}

static void push_msg(
    Vorder_book *dut,
    uint8_t msg_type,
    bool is_buy,
    uint16_t stock_locate,
    uint64_t order_ref,
    uint32_t shares,
    uint32_t price,
    int max_wait = 2000
){
    REQUIRES(dut != nullptr);
    REQUIRES(dut->rst_n == 1);
    //

    setup_msg(dut, msg_type, is_buy, stock_locate, order_ref, shares, price);

    dut->s_axis_tvalid = 1;
    
    // wait for handshake
    bool handshake = false;
    for (int i = 0; i < max_wait && !handshake; i++){
        dut->eval();
        handshake = dut->s_axis_tready;
        tick(dut);
    }
    if (!handshake){
        std::printf("FAILED: book did not accept message; order_ref: %llu\n",
                    (unsigned long long)order_ref);
        failures++;
        return;
    }

    // wait for processing
    dut->s_axis_tvalid = 0;
    for (int i = 0; i < max_wait; i++){
        dut->eval();
        if (dut->s_axis_tready){
            return;
        }
        tick(dut);
    }
    std::printf("FAILED: book did not return to idle state; order_ref: %llu\n",
                (unsigned long long)order_ref);
    failures++;
}


/* 
actual tests:
*/

// empty book should give the max/min values for bid/ask
// need to be able to see no market vs market at price 0
static void test_reset(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TEST1: empty book resets to max/min val\n");
    reset(dut);

    check(dut->best_ask_price, ASK_EMPTY, "ask baseline");
    check(dut->best_bid_price, BID_EMPTY, "bid baseline");
    check(dut->miss_count, 0, "miss_count");
    check(dut->overflow_count, 0, "overflow_count");
    check(dut->level_collision_count, 0, "level_collision_count");
}

// one Add per side (bid/ask are on diff ladders)
static void test_add_sets_topofbook(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TEST2: add sets top of book\n");
    reset(dut);

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    check(dut->best_bid_price, 1230000, "bid price");
    check(dut->best_bid_shares, 500, "bid shares");
    push_msg(dut, MSG_ADD, false, 1, 200, 300, 1230500);
    check(dut->best_ask_price, 1230500, "ask price");
    check(dut->best_ask_shares, 300, "ask shares");
    check(dut->best_bid_price, 1230000, "ask doesn't affect bid");
}


// adding should only improve topofbook, a worse price should not affect it
static void test_add_only_improves_topofbook(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TEST3: top of book does not worsen on an Add\n");
    reset(dut);

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    check(dut->best_bid_price, 1230000, "bid 1");
    push_msg(dut, MSG_ADD, true, 1, 101, 400, 1229500);
    check(dut->best_bid_price, 1230000, "worse bid ignored");
    check(dut->best_bid_shares, 500, "shares remain from best bid)");
    push_msg(dut, MSG_ADD, true, 1, 102, 600, 1231000);
    check(dut->best_bid_price, 1231000, "better bid takes top");
    check(dut->best_bid_shares, 600, "better bid's shares replaces old");
}

// if we have multiple orders at the same price they should collapse to one L2 level
static void test_add_aggregates_same_price(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TEST4: orders at same price collapse to one L2 level\n");
    reset(dut);

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    check(dut->best_bid_shares, 500, "1 order");
    push_msg(dut, MSG_ADD, true, 1, 101, 300, 1230000);
    check(dut->best_bid_price, 1230000, "price same");
    check(dut->best_bid_shares, 800, "aggregated the 2 orders in the level");
    push_msg(dut, MSG_ADD, true, 1, 102, 200, 1230000);
    check(dut->best_bid_shares, 1000, "aggregated the 3 orders in the level");
}

// if execute a partial amount of a level, shares should drop but order and L2 level stay
static void test_exc_partial_reduces_shares(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TEST5: partial filling reduces shares\n");
    reset(dut);

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    push_msg(dut, MSG_EXC, false, 1, 100, 200, 0);
    check(dut->best_bid_price, 1230000, "price survives");
    check(dut->best_bid_shares, 300, "shares reduced by the amount executed");
    check(dut->miss_count, 0, "no misses");
}

static void test_exc_full_empties_level_and_rescans(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TEST6: executed full empties the level and causes rescan for top level\n");
    reset(dut);

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000); // top
    push_msg(dut, MSG_ADD, true, 1, 101, 400, 1229500);
    check(dut->best_bid_price, 1230000, "top prior to emptying");
    push_msg(dut, MSG_EXC, false, 1, 100, 500, 0); // empties top level
    check(dut->best_bid_price, 1229500, "rescan moved 2nd best up");
    check(dut->best_bid_shares, 400, "2nd best's shares");
}

static void test_exc_unknown_ref_counts_miss(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TEST7: executing on unknown order ref causes miss to increment");
    reset(dut);

    push_msg(dut, MSG_EXC, false, 1, 123, 100, 0);
    check(dut->miss_count, 1, "execute miss counted");
    check(dut->best_bid_price, BID_EMPTY, "book not affected by miss");
}

// deleting should remove full order, and deleting last order on bid/ask should reset it to baseline
static void test_del_removes_order_and_rescans(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TEST8: delete removes full order\n");
    reset(dut);

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    push_msg(dut, MSG_ADD, true, 1, 101, 400, 1229500);
    push_msg(dut, MSG_DEL, false, 1, 100, 0, 0);
    check(dut->best_bid_price, 1229500, "top removed and 2nd best promoted");
    check(dut->best_bid_shares, 400, "next levels shares");
    push_msg(dut, MSG_DEL, false, 1, 101, 0, 0);
    check(dut->best_bid_price, BID_EMPTY, "empty book gives 0 (baseline for bid)");
}

// when we delete one of multiple orders at an L2 level, it should not delete the level - like exc
static void test_del_leaves_level_existing(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TEST9: delete leaves L2 level when there are other orders on it\n");
    reset(dut);

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    push_msg(dut, MSG_ADD, true, 1, 101, 300, 1230000);
    check(dut->best_bid_shares, 800, "aggregated shares on add");
    push_msg(dut, MSG_DEL, false, 1, 100, 0, 0);
    check(dut->best_bid_price, 1230000, "L2 level survived delete");
    check(dut->best_bid_shares, 300, "shares reduced and L2 level survived");
}

static void test_del_unknown_ref_counts_miss(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TEST10: delete on unknow order ref counts as a miss\n");
    reset(dut);

    push_msg(dut, MSG_DEL, false, 1, 123, 0, 0);
    check(dut->miss_count, 1, "delete miss counted");
    check(dut->best_bid_price, BID_EMPTY, "book not affected by miss");
}

static void test_lifecycle(Vorder_book *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TEST11: full book lifecycle\n");
    reset(dut);

    push_msg(dut, MSG_ADD, true, 1, 1, 100, 1229000);
    push_msg(dut, MSG_ADD, true, 1, 2, 200, 1229500);
    push_msg(dut, MSG_ADD, true, 1, 3, 300, 1230000);
    push_msg(dut, MSG_ADD, false, 1, 4, 400, 1230500);
    push_msg(dut, MSG_ADD, false, 1, 5, 500, 1231000);
    check(dut->best_bid_price, 1230000, "best bid price");
    check(dut->best_bid_shares, 300, "best bid shares");
    check(dut->best_ask_price, 1230500, "best ask price");
    check(dut->best_ask_shares, 400, "best ask shares");
    check(dut->best_ask_price - dut->best_bid_price, 500, "spread (to be consumed by signal engine)");
    push_msg(dut, MSG_EXC, false, 1, 4, 400, 0);
    check(dut->best_bid_price, 1230000, "ask activity shouldn't affect bid");
    check(dut->best_ask_price, 1231000, "ask moved up");
    check(dut->best_ask_shares, 500, "new ask shares");
    check(dut->overflow_count, 0, "no overflowing");
    check(dut->miss_count, 0, "no misses");
    check(dut->level_collision_count, 0, "no L2 level collisions");
}

int main(int argc, char **argv){
    Verilated::commandArgs(argc, argv);
    Vorder_book *dut = new Vorder_book;

    test_reset(dut);
    test_add_sets_topofbook(dut);
    test_add_only_improves_topofbook(dut);
    test_add_aggregates_same_price(dut);
    test_exc_partial_reduces_shares(dut);
    test_exc_full_empties_level_and_rescans(dut);
    test_exc_unknown_ref_counts_miss(dut);
    test_del_removes_order_and_rescans(dut);
    test_del_leaves_level_existing(dut);
    test_del_unknown_ref_counts_miss(dut);
    test_lifecycle(dut);

    std::printf("\n%s (Failures: %d)\n",
                failures ? "FAILED" : "PASSED", failures);
    
    dut->final(); // tells Verilator simulation is over
    delete dut; // free()

    return failures ? 1 : 0;
}