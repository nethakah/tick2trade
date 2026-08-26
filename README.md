# tick2trade

NASDAQ ITCH 5.0 market data pipeline in FPGA fabric. MoldUDP64 packets go in over
AXI4-Stream, the limit order book is reconstructed in hardware, and a preloaded
order fires when the book hits conditions software set in advance.

Running on a Xilinx ZCU104 (xczu7ev). Numbers below are measured on the board.

## Results

| | |
| :-- | :-- |
| Decision latency | 33.3 ns (8 cycles @ 240 MHz) |
| Clock, in-system | 240 MHz, +0.125 ns post-route |
| Clock, standalone | 288 MHz out-of-context |
| LUTs | 20,229 / 230,400 (8.78%) |
| Registers | 3,060 / 460,800 (0.66%) |
| BRAM / URAM / DSP | 0 / 0 / 0 |

Decision latency is last byte of the ITCH message to `order_fire` asserting. The
fabric timestamps itself with a free running counter and reports the difference
over AXI-Lite, since software can't time a 33 ns event.

Verilator reported the same 8 cycles. The nanoseconds differ only because
standalone Fmax was 288 MHz and the board runs at 240.

## Pipeline

```
DDR4 → AXI-DMA → async_fifo → moldudp_deframer → itch_parser → order_book → trade_signal
       100 MHz     CDC                        240 MHz
                                                   ↑
                                          tick2trade_csr (AXI-Lite from the PS)
```

The FIFO is the only clock domain crossing. Gray-coded pointers, two-flop
synchronisers, `ASYNC_REG` on both pairs. That last part cost me a day (bug-13).

## Two parsing stages

MoldUDP64 is transport, ITCH is content, so they're separate modules.

The deframer handles the UDP envelope and tracks sequence numbers across packets
to count gaps. The parser reads ITCH byte by byte against the spec offsets,
handles Add Order / Order Executed / Order Delete, skips everything else by
length, and filters on `stock_locate`.

Splitting them means the parser works behind anything. NASDAQ's historical files
are the same ITCH messages with a 2-byte length prefix instead of the UDP
envelope, so only the deframer would change.

## The order book

- L3 hashes `order_ref_num` into 1024 buckets, 4 entries wide. Every lookup reads
  all 4 in one access and compares in parallel, so it's constant latency
  regardless of collisions.
- L2 is a price ladder per side, 1024 slots, hashed on price.
- L1 is best bid and best ask.

All LUTRAM. The bucket is 640 bits wide and BRAM/URAM are natively 72 bits per
port, so BRAM would mean 4 sequential reads per lookup. I picked the width first
and took the memory type that follows from it.

That's also the timing ceiling. Critical path is a `curr_level` bit reaching 1080
LUTRAM address pins, 0 logic levels, 97% routing. It's a distance problem.

## The trade signal

Software preloads side, limit price, size, max spread, min resting size over
AXI-Lite. When the book satisfies all of it the fabric fires. No CPU in the path.

`cfg_armed` resets to 0, so the kill switch defaults to off and software has to
arm the hardware before it can fire anything.

## Ingest

Over AXI-DMA, not a MAC, so this measures decision latency rather than
wire-to-trade. The deframer sits behind AXI4-Stream, so a UDP stack would drop in
without touching the pipeline.

## Verification

Verilator/C++ testbenches per module plus SVA under `ifdef SIM`. Stimulus is back
to back by default, since real feeds and DMA never pause between bytes and gapped
stimulus hides FSM bugs at message boundaries.

Simulation still missed a real bug. bug-13 only appears on silicon because
Verilator has no notion of a voltage between 0 and 1.

## Build

Simulation:
```
bash scripts/lint.sh
```

Out-of-context synth and impl:
```
bash scripts/synth.sh
bash scripts/impl.sh
```

Board:
```
vivado -mode batch -source fpga/scripts/package_ip.tcl
vivado -mode batch -source fpga/scripts/create_bd.tcl
```
`fpga/ip/` is generated and gitignored, so `package_ip.tcl` runs first. Then
generate the bitstream, copy the `.bit` and `.hwh` to the board, run `sw/run.py`.

## Layout

```
rtl/         9 SystemVerilog modules
tb/          Verilator testbenches and ITCH message builders
fpga/        constraints, Tcl, timing and utilization reports
sw/          runs on the board: packet generator and PYNQ driver
build_logs/  journal.md and bugs.md
```

## Scope

One ticker. Multi-symbol means indexing everything by `stock_locate`, and at 100
symbols that's already 64 Mb, which in production means books across devices.

## Log

`build_logs/journal.md` is what I tried and why.
`build_logs/bugs.md` is symptom / cause / fix.