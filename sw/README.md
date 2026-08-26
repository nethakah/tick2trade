# Running on the ZCU104

Board is PYNQ 3.1.1, aarch64. The `.bit` and `.hwh` in `overlay/` are the exact
artifacts the 33.3 ns measurement came from.

## Deploy

From the repo root:

```
scp sw/overlay/tick2trade.bit sw/overlay/tick2trade.hwh \
    sw/gen_itch.cpp sw/run.py \
    tb/itch_messages.hpp tb/contracts.hpp \
    xilinx@<board>:~/tick2trade/
```

`gen_itch.cpp` includes `itch_messages.hpp` which includes `contracts.hpp`, so
both headers have to come along.

## Run

On the board:

```
cd ~/tick2trade
g++ -O2 -o gen_itch gen_itch.cpp && ./gen_itch itch_data.bin
sudo -E python3 run.py
```

`sudo` because programming the PL needs root. `-E` keeps the environment so sudo
finds PYNQ's virtualenv.

`gen_itch` builds the packet file with the same message builders the Verilator
testbenches use, so the bytes the board sees are identical to what simulation
saw. That's what makes `run.py` output directly comparable to integration TEST3.

## Expected

```
packet_count = 1
gap_count = 0
best_bid_price = 1230000
best_ask_price = 1230500
best_bid_shares = 500
best_ask_shares = 300
spread = 500
fire_count = 1
miss_count = 0
overflow_count = 0
level_collision = 0

Decision latency: 8 cycles (33.3 ns)
```

## Rebuilding the bitstream

Root README has the flow. `package_ip.tcl`, then `create_bd.tcl`, then generate
the bitstream in Vivado. The outputs are at:

```
<proj>.runs/impl_1/tick2trade_bd_wrapper.bit
<proj>.gen/sources_1/bd/tick2trade_bd/hw_handoff/tick2trade_bd.hwh
```

If you change `pl_clk1`, update `CORE_PERIOD_NS` in `run.py` to match or the
latency print will be wrong.