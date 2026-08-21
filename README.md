# tick2trade

A NASDAQ ITCH 5.0 tick-to-signal pipeline in SystemVerilog, targeting a Xilinx ZCU104 (Zynq UltraScale+ XCZU7EV-2). Raw MoldUDP64 packets in, order book state and a trading trigger out.

**294 MHz post-route, 8.64% LUT utilization, timing closed.** All seven modules verified in Verilator with C++ testbenches and SystemVerilog assertions.

> RTL complete and synthesized. Board bring-up (AXI-DMA block design, PYNQ host software, measured latency) is in progress.

## Architecture

```
DMA ──▶ async_fifo ──▶ moldudp_deframer ──▶ itch_parser ──▶ order_book ──▶ trade_signal
        (CDC)          (strip envelope,      (decode into    (L3 + L2)      (release the
                        detect gaps)          msg_t)                         preloaded order)
```

Every stage boundary is AXI4-Stream, so the ingress source is swappable — a 10G Ethernet MAC emits the same interface the DMA does.

| Module | Purpose | Verification |
| :-- | :-- | :-- |
| `msg_pkg.sv` | Shared types, hash functions, length/decode lookups | — |
| `skid_buffer.sv` | Generic 2-slot AXI4-Stream register slice | in-context + SVA |
| `async_fifo.sv` | Dual-clock FIFO, Gray-coded pointers, two-flop synchronizers | 4 tests |
| `moldudp_deframer.sv` | Strips MoldUDP64 framing, detects sequence gaps | 2 tests |
| `itch_parser.sv` | Decodes Add / Executed / Delete into a packed struct | 6 tests, 100k randomized |
| `order_book.sv` | L3 order table + L2 price ladder + top-of-book | 11 tests |
| `trade_signal.sv` | Releases a preloaded order when its conditions hold | 10 tests |
| `tick2trade_top.sv` | Integration across two clock domains | 5 end-to-end tests |

## Results

Post-route on xczu7ev-ffvc1156-2-e, Vivado 2024.1. Reports in `fpga/results/`, reproducible via `scripts/synth.sh` and `scripts/impl.sh`.

| Metric | Value |
| :-- | :-- |
| **Fmax** | **294 MHz** (3.388 ns critical path, +0.012 ns slack at a 3.4 ns constraint) |
| Critical path | `order_ref_num` → L3 write port: 10 logic levels, 5 carry chains of address decode, 68% routing delay |
| LUTs | 19,914 / 230,400 (**8.64%**) |
| Registers | 2,615 / 460,800 (0.57%) |
| BRAM / URAM / DSP | 0 / 0 / 0 |

**Calculated tick-to-signal** at 3.388 ns/cycle: 37 parser cycles + 4 book cycles + 1 signal cycle ≈ **142 ns** for a 36-byte Add Order. This is arithmetic; the measured figure comes from hardware.

## Why two parsing stages

Market data arrives as UDP packets, not a clean byte stream:

```
[Session 10B][Sequence 8B][Count 2B][Len 2B][ITCH msg][Len 2B][ITCH msg]...
```

The **deframer** strips that envelope and emits bare ITCH bytes. The **parser** decodes each message into fields. They're separate because MoldUDP64 packs several variable-length messages per packet — message boundaries aren't known until the length prefixes are walked.

The deframer also tracks sequence numbers. UDP drops packets, and MoldUDP64 sequence numbers are monotonic, so `seq != prev_seq + prev_count` means messages were lost. That's the only way a ticker plant knows its book might be wrong.

## The order book

Execute and Delete messages carry **only `order_ref_num`** — no price, no symbol. Applying an execution to the right price level requires looking up which order that reference belongs to. That lookup is the L3 book; it isn't an optimization, it's a protocol requirement.

**L3 (order table).** XOR-folded hash of `order_ref_num` to a bucket. Each bucket holds four entries in one 640-bit word, read in a single access and compared in parallel — one memory read plus four comparators, so lookup latency is constant regardless of book depth. A tree would be O(log n), and *slowest when the book is deep*, which is exactly when message rates spike.

**L2 (price ladder).** Shares aggregated by price, maintained incrementally: every order entering or leaving adds or removes its shares from one level. Finding the best bid by scanning 4,096 orders per message isn't feasible.

**Top of book** updates in one comparison on an Add — a new order either beats the current best or is irrelevant. When the *top level empties*, the runner-up isn't tracked anywhere and a bounded 1,024-cycle scan finds it. That's the only non-constant path in the design, it's rare, and it's bounded — unlike a tree, where every lookup is load-dependent.

## The trade signal

Software decides *what* to want; hardware decides *when* to fire.

A CPU running the alpha model preloads an order — side, price, size — plus its gates into configuration registers, then walks away. The fabric watches every market update and releases that stored order the instant its conditions hold. It computes nothing; it copies out a stored decision at the right instant.

The gates are pre-trade risk checks: `cfg_size_min` prevents slippage (firing for 100 shares when only 5 are offered fills 5 well and the rest badly), `cfg_spread_max` refuses to trade into a chaotic market, and `cfg_armed` is a hardware kill switch that stops all trading in one cycle.

## Verification

Verilator with C++ testbenches, plus SystemVerilog assertions in every module.

```bash
./scripts/lint.sh

rm -rf obj_dir && verilator --cc --exe --build -j 0 --assert +define+SIM \
    --top-module itch_parser rtl/msg_pkg.sv rtl/skid_buffer.sv rtl/itch_parser.sv \
    tb/tb_itch_parser.cpp && ./obj_dir/Vitch_parser
```

Testbenches use 15-122 style `REQUIRES`/`ENSURES` contracts over `assert()`. SVA covers the AXI4-Stream handshake, book invariants (a crossed market is impossible), and pulse-width properties — assertions that would have caught three of the eleven logged bugs at the moment of violation rather than three modules downstream.

Stimulus is back-to-back by default: real feeds and DMA never pause between messages, and gapped stimulus hides last-byte/first-byte FSM bugs that otherwise only appear on hardware.

## Design notes

Every byte offset traces to a table in the Nasdaq TotalView-ITCH 5.0 specification.

**Wire format is decoded at the parser boundary.** The ASCII message type becomes a 4-bit enum, Buy/Sell becomes one bit. No downstream module knows NASDAQ chose letters. Price stays raw `Price(4)` fixed-point — integer comparison preserves ordering, so the hardware never divides.

**Backpressure is lossless end to end.** When the book stalls, pressure propagates back through the skid buffer, parser, and FIFO until the DMA pauses and data waits in DDR4. A live exchange feed cannot be backpressured and would instead require drop-and-recover using the sequence numbers the deframer already tracks.

**The book maps to distributed LUTRAM, not block memory.** The 640-bit bucket exceeds BRAM's and UltraRAM's 72-bit native port width, so neither can serve it in a single cycle. Narrowing the word to fit would require four sequential reads per lookup, trading the constant-latency property for memory type. Hot-path structures in production HFT designs make the same trade; block memory serves bulk state where sequential access is acceptable.

## Scope

**Single symbol.** Selected at runtime via `cfg_stock_locate`; messages for other symbols are dropped at ingress. ITCH carries ~8,000 symbols and no single FPGA holds books for all of them — production systems shard across devices. Multi-symbol here would index every structure by `stock_locate`, multiplying memory by symbol count and exceeding the device's 38 Mb of block memory.

**Three message types.** Add Order (`A`), Order Executed (`E`), Order Delete (`D`) — enough to maintain a book. Replace, Cancel, and Cross messages are not implemented.

**Tick-to-signal, not tick-to-trade.** The pipeline produces a trade decision; it does not send an order. Order entry requires a separate protocol (OUCH), a live session, and risk infrastructure outside the scope of this RTL.

**Ingress is DMA, not Ethernet.** The ZCU104 has no SFP+ cage — its single GTH transceiver on the FMC LPC connector is consumed by HDMI. Every stage boundary is AXI4-Stream, so substituting a 10G MAC is a port-level change.

## Trading terms

| Term | Meaning |
| :-- | :-- |
| **size** | number of shares |
| **the touch** | the best bid and best ask |
| **lifting the offer** | buying at the ask |
| **hitting the bid** | selling at the bid |
| **spread** | ask price − bid price |
| **slippage** | a worse average fill than the quote, because the order exceeded available size |
| **L3 / L2** | individual orders by reference / orders aggregated by price |
| **crossed market** | bid at or above ask — impossible, since those orders would have matched |

## Engineering log

`build_logs/journal.md` records every non-obvious decision and its reasoning — 48 entries. `build_logs/bugs.md` records eleven non-trivial bugs: symptom, cause, fix, and what class of mistake each belonged to.

Some findings worth surfacing:

- **Vivado cannot infer RAM from arrays of packed structs.** It built 598,016 flip-flops and ~56,000 muxes instead, and synthesis ground for two hours. Declaring storage as a flat bit vector and casting at the boundary cut that to 90 seconds.
- **Timing closure is not monotonic in the constraint.** Relaxing from 3.4 ns to 3.5 ns made timing *worse* — the placer stops optimizing once it believes timing is met, and the resulting path was 93% routing across three logic levels.
- **The critical path moved between synthesis and routing.** Post-synth it was the book's rescan comparison; post-route it's the L3 write port. Placement decides which path is worst, so the real bottleneck isn't visible before routing.

## Toolchain

SystemVerilog · Verilator 5.050 · C++ · Vivado 2024.1 · ZCU104 (xczu7ev-ffvc1156-2-e)