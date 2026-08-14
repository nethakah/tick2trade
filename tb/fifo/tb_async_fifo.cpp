/*
rm -rf obj_dir
verilator --cc --exe --build -j 0 --top-module async_fifo rtl/async_fifo.sv tb/fifo/tb_async_fifo.cpp
./obj_dir/Vasync_fifo
*/

#include "Vasync_fifo.h"
#include <verilated.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "../include/contracts.hpp"

static int failures = 0;

// keep coprime
static constexpr int W_PERIOD = 20;
static constexpr int R_PERIOD = 13;

static void check(uint64_t actual, uint64_t expected, const char *name){
    REQUIRES(name != nullptr);
    //

    if (actual != expected){
        std::printf("FAILED %-20s. got: 0x%llx; expected: 0x%llx\n",
                    name, (unsigned long long)actual, (unsigned long long)expected);
        failures++;
    }
}

// virtual time since clocks should toggle their own period
static uint64_t sim_time = 0;

// step() - advance 1 unit of time (toggle whatever clock should tick + return which edge occured)
struct Edges{
    bool w_rise;
    bool r_rise;
};
static Edges step(Vasync_fifo *dut){
    REQUIRES(dut != nullptr);
    //

    Edges e = {false, false};
    sim_time++;

    int w_phase = sim_time % W_PERIOD;
    int r_phase = sim_time % R_PERIOD;

    if (w_phase == 0){
        dut->w_clk = 1;
        e.w_rise = true;
    }
    else if (w_phase == W_PERIOD/2){
        dut->w_clk = 0;
    }

    if (r_phase == 0){
        dut->r_clk = 1;
        e.r_rise = true;
    }
    else if (r_phase == R_PERIOD/2){
        dut->r_clk = 0;
    }

    dut->eval();
    return e;
}

// hold reset long enough for the slower clock to see multiple edges
// "time units" not cycles
static constexpr int RESET_HOLD_UNITS = W_PERIOD * 5;
static constexpr int RESET_SETTLE_UNITS = W_PERIOD * 3;

static void reset(Vasync_fifo *dut){
    REQUIRES(dut != nullptr);
    //

    dut->w_rst_n = 0;
    dut->r_rst_n = 0;
    dut->w_enbl = 0;
    dut->r_enbl = 0;
    dut->w_data = 0;

    dut->w_clk = 0;
    dut->r_clk = 0;

    for (int i = 0; i < RESET_HOLD_UNITS; i++){
        step(dut);
    }

    dut->w_rst_n = 1;
    dut->r_rst_n = 1;

    for (int i = 0; i < RESET_SETTLE_UNITS; i++){
        step(dut);
    }

    //
    ENSURES(dut->empty == 1);
    ENSURES(dut->full == 0);
}

static bool fifo_write(Vasync_fifo *dut, uint32_t data, int max_time = W_PERIOD*4){
    REQUIRES(dut != nullptr);
    REQUIRES(data <= 0xFF); // since its an 8-bit fifo
    //

    if (dut->full){
        return false; // cannot write while full
    }

    dut->w_data = data;
    dut->w_enbl = 1;

    for (int i = 0; i < max_time; i++){
        Edges e = step(dut);
        if (e.w_rise){
            dut->w_enbl = 0;
            return true;
        }
    }

    std::printf("FAILED: no w_clk edge within %d time units\n", max_time);
    failures++;
    return false;
}

static bool fifo_read(Vasync_fifo *dut, uint32_t *out, int max_time = R_PERIOD*4){
    REQUIRES(dut != nullptr);
    REQUIRES(out != nullptr);
    //

    if (dut->empty){
        return false; // cannot read while empty
    }

    *out = dut->r_data;
    dut->r_enbl = 1;

    for (int i = 0; i < max_time; i++){
        Edges e = step(dut);
        if (e.r_rise){
            dut->r_enbl = 0;
            return true;
        }
    }

    std::printf("FAILED: no r_clk edge within %d time units\n", max_time);
    failures++;
    return false;
}

static void test_word(Vasync_fifo *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TESTING: single word cross-domain\n");
    reset(dut);

    const uint32_t VAL = 0x12;

    check(fifo_write(dut, VAL), 1, "write accepted");

    // Wait for Write pointer to cross into Read domain
    // empty asserted until w_ptr_gray shows thru both flops
    int units_waited = 0;
    while(dut->empty && units_waited < R_PERIOD*10){
        step(dut);
        units_waited++;
    }

    check(dut->empty, 0, "not empty post write crossed");
    std::printf(" (empty deasserted after %d time units, about %d r_clk cycles)\n", units_waited, units_waited/R_PERIOD);

    uint32_t got = 0;
    check(fifo_read(dut, &got), 1, "read accepted");
    check(got, VAL, "data survived crossing");

    // After reading the 1 word, read pointer croses back into write domain before settle
    for (int i = 0; i < W_PERIOD*6; i++){
        step(dut);
    }
    check(dut->empty, 1, "empty again post-read");

}

static void test_fill_drain(Vasync_fifo *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TESTING: fill to cap + drain\n");
    reset(dut);

    const int DEPTH = 16; // match the depth in the RTL!!!

    // FILLING
    for (int i = 0; i < DEPTH; i++){
        if (!fifo_write(dut, i+1)){
            std::printf("FAILED: write %d refused (became full earlier than expected)\n", i);
            failures++;
            return;
        }
    }

    // let both domains settle then check write while full
    int waited = 0;
    while (!dut->full && waited < W_PERIOD*10){
        step(dut);
        waited++;
    }
    check(dut->full, 1, "full after DEPTH writes");
    std::printf(" (full asserted %d time units after prev write)\n", waited);

    check(fifo_write(dut, 99), 0, "write refused at full");

    // DRAINING
    for (int i = 0; i < DEPTH; i++){
        int waited = 0;
        while(dut->empty && waited < R_PERIOD*10){
            step(dut);
            waited++;
        }

        uint32_t got = 0;
        if (!fifo_read(dut, &got)){
            std::printf("FAILED: read %d refused (empty earlier than expected)\n", i);
            failures++;
            return;
        }

        // for fifo ordering (word i must come out as i+1 in order written)
        if (got != (uint32_t)(i+1)){
            std::printf("FAILED: word %d out of order. got: %u; expected: %u\n", i, got, i+1);
            failures++;
        }
    }

    for (int i = 0; i < W_PERIOD*6; i++){
        step(dut);
    }
    check(dut->empty, 1, "empty post-draining");
    check(dut->full, 0, "not full post-draining");
}

// push and pop repeatedly so pointers wrap around many times
// needed on top of fill-drain because after "DEPTH" writes, the ptr wraps back to 0, which could be unsafe
// fill halfway, alternate 1 W 1 R for a long stretch (keeping FIFO occupied)
static void test_wraparound(Vasync_fifo *dut){
    REQUIRES(dut != nullptr);
    //

    std::printf("TESTING: pointer wraparound\n");
    reset(dut);

    const int DEPTH = 16; // match rtl
    const int PREFILL = DEPTH/2;
    const int CYCLES = 200;

    uint32_t next_write = 1;
    uint32_t next_read = 1;

    // PREFILL
    for (int i = 0; i < PREFILL; i++){
        if (!fifo_write(dut, next_write & 0xFF)){
            std::printf("FAILED: prefill write %d refused\n", i);
            failures++;
            return;
        }
        next_write++;
    }

    //ALTERNATE
    for (int c = 0; c < CYCLES; c++){
        int waited = 0;
        while (dut->empty && waited < R_PERIOD*10){
            step(dut);
            waited++;
        }
        if (dut->empty){
            std::printf("FAILED: still empty on cycle %d\n", c);
            failures++;
            return;
        }

        uint32_t got = 0;
        if (!fifo_read(dut, &got)){
            std::printf("FAILED: read refused on cycle %d\n", c);
            failures++;
            return;
        }
        if (got != (next_read & 0xFF)){
            std::printf("FAILED: cycle %d out of order. got: %u; expected: %u\n", c, got, next_read&0xFF);
            failures++;
            return;
        }
        next_read++;

        waited = 0;
        while(dut->full && waited < W_PERIOD*10){
            step(dut);
            waited++;
        }
        if (!fifo_write(dut, next_write&0xFF)){
            std::printf("FAILED: write refused on cycle %d\n", c);
            failures++;
            return;
        }
        next_write++;
    }

    std::printf(" (%d W/R pairs; around %d laps of a %d depth wraparound wheel)\n", CYCLES, CYCLES/DEPTH, DEPTH);
}

int main(int argc, char** argv) // (argument count (words typed), argument vector (words themselves))
{
    Verilated::commandArgs(argc, argv); // give args to the runtime
    Vasync_fifo *dut = new Vasync_fifo; // alloc'd (Device-Under-Test)

    std::printf("TESTING: reset\n");
    reset(dut);

    check(dut->empty, 1, "empty after reset");
    check(dut->full, 0, "full after reset");

    test_word(dut);
    test_fill_drain(dut);
    test_wraparound(dut);

    std::printf("\n%s (Failures: %d)\n",
                failures ? "FAILED" : "PASSED", failures);
    
    dut->final(); // tells Verilator simulation is over
    delete dut; // free()

    return failures ? 1 : 0;
}