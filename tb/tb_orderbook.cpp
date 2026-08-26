/*
rm -rf obj_dir
verilator --cc --exe --build -j 0  --assert +define+SIM --top-module order_book rtl/msg_pkg.sv rtl/order_book.sv tb/tb_orderbook.cpp
./obj_dir/Vorder_book
*/

#include "Vorder_book.h"
#include <verilated.h>
#include <cstdio>
#include <cstdint>
#include "contracts.hpp"
#include <vector>
#include "book_model.hpp"
#include <cstdlib>

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
    dut->cfg_stock_locate = 1; // all tests use stock_locate=1 here

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


static void check(uint64_t actual, uint64_t expected, const char *name){
    REQUIRES(name != nullptr);
    //
    
    if (actual != expected){
        std::printf("FAIL %-28s got: 0x%llx, expected: 0x%llx\n",
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
static void test_reset(){
    std::printf("TEST1: empty book resets to max/min val\n");
    Vorder_book *dut = fresh_dut();

    check(dut->best_ask_price, ASK_EMPTY, "ask baseline");
    check(dut->best_bid_price, BID_EMPTY, "bid baseline");
    check(dut->miss_count, 0, "miss_count");
    check(dut->overflow_count, 0, "overflow_count");
    check(dut->level_collision_count, 0, "level_collision_count");

    dut->final();
    delete dut;
}

// one Add per side (bid/ask are on diff ladders)
static void test_add_sets_topofbook(){
    std::printf("TEST2: add sets top of book\n");
    Vorder_book *dut = fresh_dut();

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    check(dut->best_bid_price, 1230000, "bid price");
    check(dut->best_bid_shares, 500, "bid shares");
    push_msg(dut, MSG_ADD, false, 1, 200, 300, 1230500);
    check(dut->best_ask_price, 1230500, "ask price");
    check(dut->best_ask_shares, 300, "ask shares");
    check(dut->best_bid_price, 1230000, "ask doesn't affect bid");

    dut->final();
    delete dut;
}


// adding should only improve topofbook, a worse price should not affect it
static void test_add_only_improves_topofbook(){
    std::printf("TEST3: top of book does not worsen on an Add\n");
    Vorder_book *dut = fresh_dut();

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    check(dut->best_bid_price, 1230000, "bid 1");
    push_msg(dut, MSG_ADD, true, 1, 101, 400, 1229500);
    check(dut->best_bid_price, 1230000, "worse bid ignored");
    check(dut->best_bid_shares, 500, "shares remain from best bid)");
    push_msg(dut, MSG_ADD, true, 1, 102, 600, 1231000);
    check(dut->best_bid_price, 1231000, "better bid takes top");
    check(dut->best_bid_shares, 600, "better bid's shares replaces old");

    dut->final();
    delete dut;
}

// if we have multiple orders at the same price they should collapse to one L2 level
static void test_add_aggregates_same_price(){
    std::printf("TEST4: orders at same price collapse to one L2 level\n");
    Vorder_book *dut = fresh_dut();

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    check(dut->best_bid_shares, 500, "1 order");
    push_msg(dut, MSG_ADD, true, 1, 101, 300, 1230000);
    check(dut->best_bid_price, 1230000, "price same");
    check(dut->best_bid_shares, 800, "aggregated the 2 orders in the level");
    push_msg(dut, MSG_ADD, true, 1, 102, 200, 1230000);
    check(dut->best_bid_shares, 1000, "aggregated the 3 orders in the level");

    dut->final();
    delete dut;
}

// if execute a partial amount of a level, shares should drop but order and L2 level stay
static void test_exc_partial_reduces_shares(){
    std::printf("TEST5: partial filling reduces shares\n");
    Vorder_book *dut = fresh_dut();

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    push_msg(dut, MSG_EXC, false, 1, 100, 200, 0);
    check(dut->best_bid_price, 1230000, "price survives");
    check(dut->best_bid_shares, 300, "shares reduced by the amount executed");
    check(dut->miss_count, 0, "no misses");

    dut->final();
    delete dut;
}

static void test_exc_full_empties_level_and_rescans(){
    std::printf("TEST6: executed full empties the level and causes rescan for top level\n");
    Vorder_book *dut = fresh_dut();

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000); // top
    push_msg(dut, MSG_ADD, true, 1, 101, 400, 1229500);
    check(dut->best_bid_price, 1230000, "top prior to emptying");
    push_msg(dut, MSG_EXC, false, 1, 100, 500, 0); // empties top level
    check(dut->best_bid_price, 1229500, "rescan moved 2nd best up");
    check(dut->best_bid_shares, 400, "2nd best's shares");

    dut->final();
    delete dut;
}

static void test_exc_unknown_ref_counts_miss(){
    std::printf("TEST7: executing on unknown order ref causes miss to increment");
    Vorder_book *dut = fresh_dut();

    push_msg(dut, MSG_EXC, false, 1, 123, 100, 0);
    check(dut->miss_count, 1, "execute miss counted");
    check(dut->best_bid_price, BID_EMPTY, "book not affected by miss");

    dut->final();
    delete dut;
}

// deleting should remove full order, and deleting last order on bid/ask should reset it to baseline
static void test_del_removes_order_and_rescans(){
    std::printf("TEST8: delete removes full order\n");
    Vorder_book *dut = fresh_dut();

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    push_msg(dut, MSG_ADD, true, 1, 101, 400, 1229500);
    push_msg(dut, MSG_DEL, false, 1, 100, 0, 0);
    check(dut->best_bid_price, 1229500, "top removed and 2nd best promoted");
    check(dut->best_bid_shares, 400, "next levels shares");
    push_msg(dut, MSG_DEL, false, 1, 101, 0, 0);
    check(dut->best_bid_price, BID_EMPTY, "empty book gives 0 (baseline for bid)");

    dut->final();
    delete dut;
}

// when we delete one of multiple orders at an L2 level, it should not delete the level - like exc
static void test_del_leaves_level_existing(){
    std::printf("TEST9: delete leaves L2 level when there are other orders on it\n");
    Vorder_book *dut = fresh_dut();

    push_msg(dut, MSG_ADD, true, 1, 100, 500, 1230000);
    push_msg(dut, MSG_ADD, true, 1, 101, 300, 1230000);
    check(dut->best_bid_shares, 800, "aggregated shares on add");
    push_msg(dut, MSG_DEL, false, 1, 100, 0, 0);
    check(dut->best_bid_price, 1230000, "L2 level survived delete");
    check(dut->best_bid_shares, 300, "shares reduced and L2 level survived");

    dut->final();
    delete dut;
}

static void test_del_unknown_ref_counts_miss(){
    std::printf("TEST10: delete on unknow order ref counts as a miss\n");
    Vorder_book *dut = fresh_dut();

    push_msg(dut, MSG_DEL, false, 1, 123, 0, 0);
    check(dut->miss_count, 1, "delete miss counted");
    check(dut->best_bid_price, BID_EMPTY, "book not affected by miss");

    dut->final();
    delete dut;
}

static void test_lifecycle(){
    std::printf("TEST11: full book lifecycle\n");
    Vorder_book *dut = fresh_dut();

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

    dut->final();
    delete dut;
}

static std::vector<uint64_t> live_refs;
static constexpr size_t MAX_LIVE_ORDERS = 300;
static constexpr uint32_t PRICE_BASE = 1230000;
static constexpr uint32_t PRICE_SPAN = 20;

static void forget_ref(uint64_t ref){
    for (size_t i = 0; i < live_refs.size(); i++){
        if (ref == live_refs[i]){
            // swap w last elem + pop
            live_refs[i] = live_refs.back();
            live_refs.pop_back();
            return;
        }
    }
}

static void test_random_against_model(int num_msgs, unsigned int seed){
    std::printf("TEST12: %d random messages against the reference model\n", num_msgs);
    Vorder_book *dut = fresh_dut();
    BookModel model;

    live_refs.clear();
    uint64_t next_ref = 1;
    srand(seed);

    for (int n = 0; n < num_msgs; n++){
        int x = rand() % 100;

        if (live_refs.empty()){
            x = 0;
        }
        else if (x<50 && live_refs.size()>=MAX_LIVE_ORDERS){
            x = 50;
        }

        if (x < 50){
            // add
            uint64_t ref = next_ref++;
            bool is_buy = (rand() % 2) == 0;
            
            uint32_t shares = 1 + (rand() % 1000);
            uint32_t price;
            // make sure we dont randomly pick a crossed market
            if (is_buy){
                price = PRICE_BASE - 1 - (rand() % PRICE_SPAN);
            }
            else{
                price = PRICE_BASE + (rand() % PRICE_SPAN);
            }

            push_msg(dut, MSG_ADD, is_buy, 1, ref, shares, price);
            model.add_order(ref, is_buy, price, shares);
            live_refs.push_back(ref);
        }
        else if (x < 75){
            // exc
            size_t i = rand() % live_refs.size();
            uint64_t ref = live_refs[i];
            uint32_t shares = 1 + (rand() % 1200);
            
            push_msg(dut, MSG_EXC, false, 1, ref, shares, 0);
            model.exc_order(ref, shares);

            if (!model.has_order(ref)){
                forget_ref(ref);
            }
        }
        else if (x < 90){
            // del
            size_t i = rand() % live_refs.size();
            uint64_t ref = live_refs[i];

            push_msg(dut, MSG_DEL, false, 1, ref, 0, 0);
            model.del_order(ref);
             
            forget_ref(ref);
        }
        else{
            // miss
            uint64_t ref = next_ref + 1000000; // go far past anything issued
            if (rand() % 2){
                push_msg(dut, MSG_EXC, false, 1, ref, 100, 0);
                model.exc_order(ref, 100);
            }
            else{
                push_msg(dut, MSG_DEL, false, 1, ref, 0, 0);
                model.del_order(ref);
            }
        }

        if (dut->best_bid_price != model.best_bid_price() || 
            dut->best_bid_shares != model.best_bid_shares() ||
            dut->best_ask_price != model.best_ask_price() ||
            dut->best_ask_shares != model.best_ask_shares() ||
            dut->miss_count != model.get_miss_count()){
            
            std::printf("FAILED! message: %d (seed: %u)\n", n, seed);
            std::printf("Bid price; dut %u; model %u\n",
                        (unsigned)dut->best_bid_price, (unsigned)model.best_bid_price());
            std::printf("Bid shares; dut %u; model %u\n",
                        (unsigned)dut->best_bid_shares, (unsigned)model.best_bid_shares());
            std::printf("Ask price; dut %u; model %u\n",
                        (unsigned)dut->best_ask_price, (unsigned)model.best_ask_price());
            std::printf("Ask shares; dut %u; model %u\n",
                        (unsigned)dut->best_ask_shares, (unsigned)model.best_ask_shares());
            std::printf("miss_count; dut %u; model %u\n",
                        (unsigned)dut->miss_count, (unsigned)model.get_miss_count());
            std::printf("live orders %zu; overflow_count %u\n",
                        live_refs.size(), (unsigned)dut->overflow_count);
            

            failures++;
            break;
        }
    }
    check(dut->overflow_count, 0, "no overflow");
    check(dut->level_collision_count, 0, "no level collisions");

    //
    dut->final();
    delete dut;
}

static void test_bucket_overflow(){
    std::printf("TEST13: 5th order in 1 bucket should cause overflow\n");
    Vorder_book *dut = fresh_dut();

    uint64_t refs[5] = {7, 1031, 2055, 3079, 4103};
    for (int i = 0; i < 4; i++){
        push_msg(dut, MSG_ADD, true, 1, refs[i], 100, 1230000 - i);
    }

    check(dut->overflow_count, 0, "4/4 fit");
    check(dut->best_bid_price, 1230000, "best bid from the first add");
    check(dut->best_bid_shares, 100, "corresponding shares");
    push_msg(dut, MSG_ADD, true, 1, refs[4], 987, 1231000);
    check(dut->overflow_count, 1, "5th order overflows");
    check(dut->best_bid_price, 1230000, "dropped order doesn't reach book");
    push_msg(dut, MSG_DEL, false, 1, refs[0], 0, 0);
    check(dut->miss_count, 0, "first order findable after overflow");
    check(dut->best_bid_price, 1229999, "2nd best got promoted");

    dut->final();
    delete dut;
}

int main(int argc, char **argv){
    Verilated::commandArgs(argc, argv);

    test_reset();
    test_add_sets_topofbook();
    test_add_only_improves_topofbook();
    test_add_aggregates_same_price();
    test_exc_partial_reduces_shares();
    test_exc_full_empties_level_and_rescans();
    test_exc_unknown_ref_counts_miss();
    test_del_removes_order_and_rescans();
    test_del_leaves_level_existing();
    test_del_unknown_ref_counts_miss();
    test_lifecycle();
    test_random_against_model(10000, 12345);
    test_random_against_model(10000, 11111);
    test_random_against_model(10000, 9);
    test_bucket_overflow();

    std::printf("\n%s (Failures: %d)\n",
                failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}