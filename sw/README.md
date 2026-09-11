# Running on the ZCU104
Board is PYNQ 3.1.1, aarch64.

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
cd <proj>
g++ -O2 -o gen_itch gen_itch.cpp
```

1 packet with a bid and an ask:
```
./gen_itch itch_data.bin
sudo -E python3 run.py
```

Full regression, 20k random messages against the reference model:
```
./gen_itch itch_data.bin 20000 12345
sudo -E python3 run.py
```

`gen_itch` drives `book_model.hpp` as it builds the stream and writes the final book state to `expected.txt`.
`run.py` reads that and compares every counter after the DMA. Ends in `PASSED!` or a list of mismatches.

One DMA transfer per MoldUDP64 packet, because the deframer treats `tlast` as end-of-packet and the DMA's length register caps a transfer at 16383 bytes.

## Notes
Rebuilding the bitstream is in the root README. Outputs land at:

```
<proj>.runs/impl_1/tick2trade_bd_wrapper.bit
<proj>.gen/sources_1/bd/tick2trade_bd/hw_handoff/tick2trade_bd.hwh
```
Note: If you change `pl_clk1`, update `CORE_PERIOD_NS` in `run.py` or the latency print
is wrong.
