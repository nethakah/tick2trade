# tick2trade

`SystemVerilog` · `Verilator` · `Vivado 2024.1` · `PYNQ` · `Xilinx ZCU104 (xczu7ev)`

NASDAQ ITCH 5.0 market data pipeline in FPGA fabric. MoldUDP64 packets in over
AXI4-Stream, limit order book reconstructed in hardware, and a preloaded order
fires when the book hits conditions software set in advance.

Every number here is measured on the board.

| | |
| :-- | :-- |
| Decision latency | 33.3 ns (8 cycles @ 240 MHz) |
| Clock, in-system | 240 MHz, +0.130 ns post-route |
| Clock, standalone | 288 MHz out-of-context |
| LUTs | 20,229 / 230,400 (8.78%) |
| Registers | 3,060 / 460,800 (0.66%) |
| BRAM / URAM / DSP | 0 / 0 / 0 |

Decision latency is last byte of the ITCH message to `order_fire` asserting. The
fabric timestamps itself and reports over AXI-Lite, since software can't time a
33 ns event. Verilator reports the same 8 cycles.

```
DDR4 → AXI-DMA → async_fifo → moldudp_deframer → itch_parser → order_book → trade_signal
       100 MHz     CDC                        240 MHz
                                                   ↑
                                          tick2trade_csr (AXI-Lite from the PS)
```

The order book is three levels, all LUTRAM. L3 hashes `order_ref_num` into 1024
buckets 4 wide, compared in parallel so lookup is constant latency. L2 is a price
ladder per side. L1 is best bid and ask. Buckets are 640 bits and BRAM is 72 bits
per port, so BRAM would have meant 4 sequential reads per lookup.

## Layout

```
rtl/
    msg_pkg.sv              shared types, hashes, ITCH message lengths
    skid_buffer.sv          AXI4-Stream pipeline register
    async_fifo.sv           the CDC, gray pointers + ASYNC_REG
    moldudp_deframer.sv     UDP envelope, sequence tracking, gap count
    itch_parser.sv          ITCH bytes to a struct, filters on stock_locate
    order_book.sv           L3 hash buckets, L2 ladders, L1 top of book
    trade_signal.sv         fire conditions and kill switch
    tick2trade_csr.sv       AXI-Lite register file
    tick2trade_top.sv       wires it together
tb/
    book_model.hpp          golden reference model of the book
    itch_messages.hpp       message builders, shared with sw/
    tb_*.cpp                one Verilator testbench per module
fpga/
    constraints/            OOC constraints and the block design CDC file
    scripts/                package_ip.tcl, create_bd.tcl, synth, impl
    results/                timing and utilization reports
sw/
    gen_itch.cpp            packet generator, runs on the board
    run.py                  PYNQ driver, programs the PL and reads counters
    deploy.sh               copies everything the board needs
    overlay/                the .bit and .hwh these numbers came from
build_logs/
    journal.md              what I tried and why
    bugs.md                 symptom / cause / fix
```

## Verification

Verilator/C++ testbench per module, SVA under `ifdef SIM`, and `tb/book_model.hpp`
as a golden reference model - std::map, no hashing, no capacity limit, so a bug in
the design can't hide in the model too. 30k random messages across three seeds in
simulation, 20k through the same generator on hardware.

Two bugs got past simulation for opposite reasons. bug-13 was a CDC failure only
visible on silicon. bug-15 was in Verilator all along but my testbenches fed the
book one message at a time, so it never backpressured and never dropped anything.

## Quickstart

Requires Verilator, and Vivado 2024.1 for the board flow.

1. Simulate:
```
    bash scripts/lint.sh
```
2. Out-of-context synth and impl:
```
    bash scripts/synth.sh
    bash scripts/impl.sh
```
3. Package the RTL as IP (`fpga/ip/` is generated and gitignored):
```
    vivado -mode batch -source fpga/scripts/package_ip.tcl
```
4. In Vivado, with `ip_repo_paths` pointed at `fpga/ip/`:
```
    source fpga/scripts/create_bd.tcl
    add_files -fileset constrs_1 fpga/constraints/tick2trade_bd.xdc
```
    `create_bd.tcl` does NOT carry that constraint file. Skip it and you get a
    bitstream with no CDC constraint and bug-13 comes back, silently.
5. Generate the bitstream, then:
```
    bash sw/deploy.sh user@board
```
    Board instructions in [sw/README.md](sw/README.md).

## Roadmap

- [x] MoldUDP64 deframer with sequence gap detection
- [x] ITCH 5.0 parser (Add / Executed / Delete)
- [x] Three-level order book in LUTRAM
- [x] Trade signal with preloaded order and kill switch
- [x] Golden reference model and randomized regression
- [x] Running on ZCU104 over AXI-DMA
- [ ] Replay real NASDAQ ITCH dumps
- [ ] Scatter-gather DMA for a real throughput number
- [ ] Ethernet front end, so it's wire-to-trade
- [ ] Multi-symbol

Ingest is over AXI-DMA rather than a MAC, so this measures decision latency, not
wire-to-trade. The deframer sits behind AXI4-Stream so a UDP stack drops in
without touching the pipeline.