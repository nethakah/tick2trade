# Running on the ZCU104

Board is PYNQ 3.1.1, aarch64. The `.bit` and `.hwh` in `overlay/` are the exact
artifacts every number in the root README came from.

## Deploy

From the repo root:

```
bash sw/deploy.sh xilinx@<board>
```

Copies the overlay, `gen_itch.cpp`, `run.py`, and the three headers from `tb/`.
The headers get pulled from `tb/` rather than duplicated here, so the board
always builds against the same message builders the Verilator testbenches use.
That's what makes the bytes the board sees identical to what simulation saw.

## Run

On the board:

```
cd ~/nhaldo/tick2trade
g++ -O2 -o gen_itch gen_itch.cpp
```

`sudo` because programming the PL needs root, `-E` so sudo keeps the environment
and finds PYNQ's virtualenv.

Smoke test, one packet with a bid and an ask:

```
./gen_itch itch_data.bin
sudo -E python3 run.py
```

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

Full regression, 20k random messages against the golden model:

```
./gen_itch itch_data.bin 20000 12345
sudo -E python3 run.py
```

`gen_itch` drives `book_model.hpp` as it builds the stream and writes the final
book state to `expected.txt`. `run.py` reads that and compares every counter
after the DMA. Ends in `PASSED!` or a list of mismatches.

One DMA transfer per MoldUDP64 packet, because the deframer treats `tlast` as
end-of-packet and the DMA's length register caps a transfer at 16383 bytes.

## Notes

Rebuilding the bitstream is in the root README. Outputs land at:

```
<proj>.runs/impl_1/tick2trade_bd_wrapper.bit
<proj>.gen/sources_1/bd/tick2trade_bd/hw_handoff/tick2trade_bd.hwh
```

If you change `pl_clk1`, update `CORE_PERIOD_NS` in `run.py` or the latency print
is wrong.