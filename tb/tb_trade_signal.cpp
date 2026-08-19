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
    ENSURES(dut->rst_n == 1);
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

static Vtrade_signal *fresh_dut(){
    Vtrade_signal *dut = new Vtrade_signal;
    reset(dut);

    ENSURES(dut != nullptr);
    return dut;
}

static void preload(
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

static bool market_update(
    Vtrade_signal *dut,
    uint32_t bid_price,
    uint32_t bid_shares,
    uint32_t ask_price,
    uint32_t ask_shares
){
    REQUIRES(dut != nullptr);
    REQUIRES(dut->rst_n == 1);
    //
    dut->best_bid_price = bid_price;
    dut->best_bid_shares = bid_shares;
    dut->best_ask_price = ask_price;
    dut->best_ask_shares = ask_shares;

    dut->book_valid = 1;
    tick(dut);
    dut->book_valid = 0;
    dut->eval(); // settle w no inputs (dont advance clock - timing bug)
    bool fired = (dut->order_fire != 0); // order_fire is HI
    
    tick(dut); // let pulse clear
    return fired;
}

// with nothing preloaded/armed, the module shouldn't do anything despite market
static void test_reset_silent(){
    std::printf("TEST1: unarmed never fires\n");
    Vtrade_signal *dut = fresh_dut();

    check(dut->order_fire, 0, "no fire post-reset");
    check(dut->fire_count, 0, "fire_count init at 0");
    preload(dut, false, true, 1230500, 100, 1000, 50);
    check(market_update(dut, 1230000, 5000, 1230500, 500), false, "no fire if unarmed");

    dut->final();
    delete dut;
}

// software preloads a buy, market hits that price, order fires with the preloaded values
static void test_buy_fires_at_price(){
    std::printf("TEST2: preloaded buy fires when there's an ask at our trigger\n");
    Vtrade_signal *dut = fresh_dut();
    
    preload(dut, true, true, 1230500, 100, 1000, 50);
    check(market_update(dut, 1230000, 5000, 1231000, 500), false, "ask > trigger");
    check(dut->fire_count, 0, "no fire");
    check(market_update(dut, 1230000, 5000, 1230500, 500), true, "ask <= trigger, fire");
    check(dut->fire_count, 1, "fire_count++");
    check(dut->order_side, 1, "order_side = buy");
    check(dut->order_price, 1230500, "order_price = our limit");
    check(dut->order_shares, 100, "order_shares = preloaded size");

    dut->final();
    delete dut;
}

// software preloads a sell, market hits that price, order fires with preloaded values
static void test_sell_fires_at_price(){
    std::printf("TEST3: preloaded sell fires when there's an ask at our trigger\n");
    Vtrade_signal *dut = fresh_dut();
    
    preload(dut, true, false, 1230000, 200, 1000, 50);
    check(market_update(dut, 1229500, 5000, 1230500, 500), false, "bid < limit");
    check(market_update(dut, 1230000, 5000, 1230500, 500), true, "bid = limit, fire");
    check(dut->order_side, 0, "order_side = sell");
    check(dut->order_shares, 200, "order_shares = preloaded size");

    dut->final();
    delete dut;
}

// if market is better than our limit we'll fire but KEEP OUR PRICE
static void test_better_price_uses_our_limit(){
    std::printf("TEST4: market better than our limit will still use our price\n");
    Vtrade_signal *dut = fresh_dut();

    preload(dut, true, true, 1230500, 100, 1000, 50);
    check(market_update(dut, 1230000, 5000, 1230100, 500), true, "better ask, fires");
    check(dut->order_price, 1230500, "order uses our limit, not market's");

    dut->final();
    delete dut;
}

// do not fire when there isn't enough shares to fill us
static void test_size_requirement(){
    std::printf("TEST5: don't fire if there is insufficient size\n");
    Vtrade_signal *dut = fresh_dut();

    preload(dut, true, true, 1230500, 100, 1000, 100);
    check(market_update(dut, 1230000, 5000, 1230500, 5), false, "5 shares offered");
    check(dut->fire_count, 0, "no fire (5<100 shares needed)");
    check(market_update(dut, 1230000, 5000, 1230500, 100), true, "enough shares, fire");
    
    dut->final();
    delete dut;
}

// if there's a wide spread then some pre-trade risk gate
static void test_spread_requirement(){
    std::printf("TEST6: don't fire if wide spread\n");
    Vtrade_signal *dut = fresh_dut();

    preload(dut, true, true, 1230500, 100, 200, 50);

    check(market_update(dut, 1220000, 5000, 1230500, 500), false, "spread too big");
    check(market_update(dut, 1230400, 5000, 1230500, 500), true, "small spread, fire");

    dut->final();
    delete(dut);
}

// unarm should stop trading in the cycle even if mid-stream
static void test_kill_switch(){
    std::printf("TEST7: unarm stops trade immediately\n");
    Vtrade_signal *dut = fresh_dut();

    preload(dut, true, true, 1230500, 100, 1000, 50);
    check(market_update(dut, 1230000, 5000, 1230500, 500), true, "armed, fires");
    check(dut->fire_count, 1, "fire_count=1");
    dut->cfg_armed = 0;
    check(market_update(dut, 1230000, 5000, 1230500, 500), false, "unarmed blocks fire");
    check(dut->fire_count, 1, "fire_count unchanged");
    dut->cfg_armed = 1;
    check(market_update(dut, 1230000, 5000, 1230500, 500), true, "re-armed, fires");
    check(dut->fire_count, 2, "fire_count++");

    dut->final();
    delete dut;
}

// empty book should have baseline values for bid/ask, and comparing against means market doesn't exist
static void test_empty_market_never_fires(){
    std::printf("TEST8: empty market should not fire\n");
    Vtrade_signal *dut = fresh_dut();

    preload(dut, true, true, 0xFFFFFFFF, 100, 0xFFFFFFFF, 0);
    check(market_update(dut, 1230000, 5000, ASK_EMPTY, 0), false, "no ask side");
    check(market_update(dut, BID_EMPTY, 0, 1230500, 500), false, "no bid side");
    check(market_update(dut, BID_EMPTY, 0, ASK_EMPTY, 0), false, "no market");
    check(dut->fire_count, 0, "no fires on empty book");

    dut->final();
    delete dut;
}

// 1 order per market event (purpose of book_valid tested here)
static void test_one_fire_per_update(){
    std::printf("TEST9: 1 fire per book update NOT 1 per cycle (this happens if book_valid broken)\n");
    Vtrade_signal *dut = fresh_dut();

    preload(dut, true, true, 1230500, 100, 1000, 50);
    dut->best_bid_price = 1230000;
    dut->best_ask_price = 1230500;
    dut->best_bid_shares = 5000;
    dut->best_ask_shares = 500;
    dut->book_valid = 0;

    for (int i = 0; i < 20; i++){
        tick(dut);
        if (dut->order_fire){
            std::printf("FAILED: fired even though no book update\n");
            failures++;
            break;
        }
    }
    check(dut->fire_count, 0, "no fires with no update (even though conditions hold)");

    dut->book_valid = 1;
    tick(dut);
    dut->book_valid = 0;
    for (int i = 0; i < 10; i++){
        tick(dut); // settle
    }
    check(dut->fire_count, 1, "1 fire per 1 update");

    dut->final();
    delete dut;
}

// spread should be computed in the RTL and observable for the software
static void test_spread_readable(){
    std::printf("TEST10: spread is computed and available\n");
    Vtrade_signal *dut = fresh_dut();

    preload(dut, false, true, 0, 0, 0, 0);
    dut->best_bid_price = 1230000;
    dut->best_ask_price = 1230500;
    dut->eval();
    check(dut->spread, 500, "spread = ask - bid");
    dut->best_ask_price = ASK_EMPTY;
    dut->eval();
    check(dut->spread, 0xFFFFFFFF, "max spread if no market");
    
    dut->final();
    delete dut;
}


int main(int argc, char **argv){
    Verilated::commandArgs(argc, argv);

    test_reset_silent();
    test_buy_fires_at_price();
    test_sell_fires_at_price();
    test_better_price_uses_our_limit();
    test_size_requirement();
    test_spread_requirement();
    test_kill_switch();
    test_empty_market_never_fires();
    test_one_fire_per_update();
    test_spread_readable();

    std::printf("\n%s (Failures: %d)\n",
                failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}