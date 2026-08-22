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