# tick2trade

A NASDAQ ITCH 5.0 tick-to-signal pipeline in SystemVerilog, built for a Xilinx ZCU104 (Zynq UltraScale+ XCZU7EV-2). Raw MoldUDP64 packets go in one end, order book state and a trading trigger come out the other.

Closed timing at **288 MHz post-route** using **8.78% of the device's LUTs**. All eight modules verified in Verilator with C++ testbenches plus SystemVerilog assertions.

> RTL is done and synthesized. Board bring-up (AXI-DMA block design, PYNQ host software, measured latency) is what I'm working on now.

## Architecture

```
DMA ──▶ async_fifo ──▶ moldudp_deframer ──▶ itch_parser ──▶ order_book ──▶ trade_signal
        (CDC)          (strip envelope,      (decode into    (L3 + L2)      (release the
                        detect gaps)          msg_t)                         preloaded order)
                                                    ▲
                        AXI4-Lite ──▶ tick2trade_csr ┘
                                      (config in, status out)
```

Every stage boundary is AXI4-Stream, so swapping the ingress source is a port-level change. A 10G Ethernet MAC emits the same interface the DMA does.

| Module | What it does | Verification |
| :-- | :-- | :-- |
| `msg_pkg.sv` | Shared types, hash functions, length/decode lookups | — |
| `skid_buffer.sv` | Generic 2-slot AXI4-Stream register slice | in-context + SVA |
| `async_fifo.sv` | Dual-clock FIFO, Gray-coded pointers, two-flop synchronizers | 4 tests |
| `moldudp_deframer.sv` | Strips MoldUDP64 framing, detects sequence gaps | 2 tests |
| `itch_parser.sv` | Decodes Add / Executed / Delete into a packed struct | 6 tests, 100k randomized |
| `order_book.sv` | L3 order table + L2 price ladder + top of book | 11 tests |
| `trade_signal.sv` | Releases a preloaded order when its conditions hold | 10 tests |
| `tick2trade_csr.sv` | AXI4-Lite control/status registers | 8 tests |
| `tick2trade_top.sv` | Integration across two clock domains | 5 end-to-end tests |

## Protocols

Two AXI variants, doing different jobs.

**AXI4-Stream** carries the data path. It's a one-way flow with a two-signal handshake: the producer raises `tvalid` when it has data, the consumer raises `tready` when it can take it, and a transfer happens on a cycle where both are high. Once `tvalid` goes up it stays up with stable `tdata` until accepted, which is what makes backpressure work. A stalled consumer just holds `tready` low and the whole pipeline waits.

**AXI4-Lite** carries the control plane. It's memory-mapped: software writes to an address and a register in fabric changes. Five channels, each with its own valid/ready pair.

```
write:  AW (address) ──┐
                       ├──▶ commit ──▶ B (response)
        W  (data)   ───┘

read:   AR (address) ──▶ R (data)
```

The catch is that AW and W are **independent**. The master can send the address several cycles before the data, so the slave latches each as it arrives and commits only once both are held. Getting that wrong produces a design that works when the two happen to arrive together and fails when they don't, which is the kind of bug that only shows up on hardware.

`tick2trade_csr.sv` is the AXI-Lite slave. It maps the trade signal's configuration onto addresses software can write, and the pipeline's status counters onto addresses software can read:

| Offset | Register | Access |
| :-- | :-- | :-- |
| `0x00` | control: `[0]` armed, `[1]` side | RW |
| `0x04` | trigger price | RW |
| `0x08` | order shares | RW |
| `0x0C` | max spread | RW |
| `0x10` | min resting size | RW |
| `0x14` | stock locate | RW |
| `0x20`–`0x30` | top of book, spread | RO |
| `0x34`–`0x48` | fire / packet / gap / miss / overflow / collision counters | RO |

Addresses step by four because every register is a 32-bit word and AXI-Lite is byte-addressed, so the register index is `addr[7:2]`.

On hardware those counters are the only visibility there is. There's no printf on an FPGA, so `gap_count` tells you UDP dropped packets, `miss_count` tells you the book saw an execution for an order it never had, and `fire_count` proves the pipeline did work.

`cfg_armed` resets to 0. The kill switch defaults to off, so the fabric physically cannot fire until software explicitly arms it, and an assertion enforces that it can only rise on a committed write.

## Results

Post-route on xczu7ev-ffvc1156-2-e with Vivado 2024.1. Reports are in `fpga/results/` and reproducible with `scripts/synth.sh` and `scripts/impl.sh`.

| Metric | Value |
| :-- | :-- |
| **Fmax** | **288 MHz** (3.476 ns critical path, +0.024 ns slack against a 3.5 ns constraint) |
| Critical path | `curr_msg.order_ref_num` to the L3 write port: 9 logic levels, 4 carry chains of address decode, 72% routing delay |
| LUTs | 20,229 / 230,400 (**8.78%**) |
| Registers | 2,836 / 460,800 (0.62%) |
| BRAM / URAM / DSP | 0 / 0 / 0 |

Calculated tick-to-signal at 3.476 ns per cycle: 37 parser cycles + 4 book cycles + 1 signal cycle, so roughly **146 ns** for a 36-byte Add Order. That's arithmetic though, not a measurement. The real number comes off the board.

The critical path has landed in the same place across every run: the L3 memory write port, where carry chains decode one of 1,024 buckets across 1,280 distributed RAM primitives. Which register sources it moves with placement, the bottleneck doesn't. That decode is the direct cost of building a 640-bit-wide memory out of LUTs.

## Why there are two parsing stages

Market data doesn't arrive as a clean byte stream, it arrives as UDP packets:

```
[Session 10B][Sequence 8B][Count 2B][Len 2B][ITCH msg][Len 2B][ITCH msg]...
```

The deframer strips that envelope and hands the parser bare ITCH bytes. The parser decodes each message into fields. They're separate because MoldUDP64 packs several variable-length messages into one packet, so you can't know where a message starts until you've walked the length prefixes.

The deframer also tracks sequence numbers. UDP drops packets, and MoldUDP64 sequence numbers are monotonic, so if `seq != prev_seq + prev_count` then messages went missing. That's the only way a ticker plant knows its book might be wrong.

## The order book

Order Executed and Order Delete carry **only `order_ref_num`**. No price, no symbol. So to apply an execution to the right price level you first have to look up which order that reference belongs to. That lookup IS the L3 book. It's not an optimization, it's a protocol requirement.

**L3 (order table).** XOR-fold the order reference into a bucket index. Each bucket holds four entries in one 640-bit word, so a single memory read pulls all four and four comparators check them in parallel. Lookup latency is constant no matter how deep the book gets. The Columbia paper uses an AVL tree here, which is O(log n) and therefore SLOWEST when the book is deep, which is exactly when message rates spike. That's the wrong shape for HFT, where variance is unhedgeable.

**L2 (price ladder).** Shares aggregated by price, maintained incrementally so every order entering or leaving adds to or subtracts from exactly one level. Scanning 4,096 orders per message to find the best bid isn't feasible.

**Top of book** updates in one comparison on an Add, since a new order either beats the current best or doesn't matter. The awkward case is when the top level EMPTIES, because the runner-up isn't tracked anywhere and a bounded 1,024-cycle scan has to find it. That's the only non-constant path in the design. It's rare and it's bounded, unlike a tree where every single lookup is load-dependent.

## The trade signal

Software decides what to want. Hardware decides when to fire.

A CPU running the alpha model preloads an order (side, price, size) plus its gates into config registers, then walks away. The fabric watches every market update and releases that stored order the instant the conditions hold. It computes nothing. It copies out a decision that was already made.

The gates are real pre-trade risk checks. `cfg_size_min` stops slippage (firing for 100 shares when only 5 are offered fills 5 well and the other 95 badly). `cfg_spread_max` refuses to trade into a chaotic market. `cfg_armed` is a hardware kill switch that stops everything in one cycle.

## Verification

Verilator with C++ testbenches, plus SystemVerilog assertions in every module.

```bash
./scripts/lint.sh

rm -rf obj_dir && verilator --cc --exe --build -j 0 --assert +define+SIM \
    --top-module itch_parser rtl/msg_pkg.sv rtl/skid_buffer.sv rtl/itch_parser.sv \
    tb/tb_itch_parser.cpp && ./obj_dir/Vitch_parser
```

The testbenches use 15-122 style `REQUIRES` / `ENSURES` contracts over `assert()`. The SVA covers the AXI handshakes on both protocols, book invariants (a crossed market is impossible, since those orders would have matched), and pulse-width properties. Three of the twelve bugs I logged would have been caught by those assertions at the moment they happened instead of showing up three modules downstream.

Stimulus is back-to-back by default. Real feeds and DMA never pause between messages, and gapped stimulus hides last-byte/first-byte FSM bugs that only turn up on hardware.

The integration test configures the pipeline over AXI-Lite rather than poking `cfg_*` directly, so it exercises the exact path the host software will use.

## Design notes

Every byte offset in the RTL traces to a table in the Nasdaq TotalView-ITCH 5.0 spec.

**Wire format gets decoded at the parser boundary.** The ASCII message type becomes a 4-bit enum and Buy/Sell becomes a single bit, so nothing downstream knows NASDAQ chose letters. Price stays as raw `Price(4)` fixed-point because integer comparison preserves ordering, which means the hardware never has to divide.

**Backpressure is lossless end to end.** When the book stalls it propagates back through the skid buffer, the parser, and the FIFO until the DMA pauses and the data just sits in DDR4. A live exchange feed can't be backpressured, so that version would need drop-and-recover using the sequence numbers the deframer already tracks.

**The book ended up in distributed LUTRAM, not block memory.** I originally planned UltraRAM for the L3, but that reasoning was about capacity when the actual constraint is port width. BRAM and URAM are both natively 72 bits wide and my bucket is 640, so neither can serve it in a single cycle. Narrowing the word to fit would mean four sequential reads per lookup, which trades away the constant-latency property to gain a memory type. Turns out hot-path structures in real HFT designs make the same trade, and block memory serves bulk state where sequential access is fine.

## Scope

**One symbol.** Selected at runtime with `cfg_stock_locate`, and messages for other symbols get dropped at ingress. ITCH carries around 8,000 symbols and no single FPGA holds books for all of them, so production systems shard across devices. Doing it here would mean indexing every structure by `stock_locate`, which multiplies memory by symbol count and blows past the device's 38 Mb of block memory.

**Three message types.** Add Order (`A`), Order Executed (`E`), Order Delete (`D`). That's enough to maintain a book. Replace, Cancel, and Cross aren't implemented.

**Tick-to-signal, not tick-to-trade.** The pipeline produces a decision, it doesn't send an order. Order entry needs a separate protocol (OUCH), a live session, and risk infrastructure that isn't an RTL problem.

**Ingress is DMA, not Ethernet.** The ZCU104 has no SFP+ cage and its single GTH transceiver on the FMC LPC connector is taken by HDMI. Since every stage boundary is AXI4-Stream, dropping in a 10G MAC instead would be a port-level substitution.

## Trading terms

| Term | What it means |
| :-- | :-- |
| **size** | number of shares |
| **the touch** | the best bid and the best ask |
| **lifting the offer** | buying at the ask |
| **hitting the bid** | selling at the bid |
| **spread** | ask price minus bid price |
| **slippage** | getting a worse average fill than the quote, because your order was bigger than the size available |
| **L3 / L2** | individual orders by reference number / orders aggregated by price |
| **crossed market** | bid at or above ask, which is impossible since those orders would have matched |

## Engineering log

`build_logs/journal.md` covers every non-obvious decision and why I made it. `build_logs/bugs.md` has twelve non-trivial bugs with symptom, cause, fix, and what class of mistake each one was.

A few findings worth pulling out:

- **Vivado can't infer RAM from arrays of packed structs.** It built 598,016 flip-flops and around 56,000 muxes instead, and synthesis ground away for over two hours. Declaring the storage as a flat bit vector and casting at the boundary took it down to 90 seconds.
- **Timing closure isn't monotonic in the constraint.** Relaxing from 3.4 ns to 3.5 ns once made timing WORSE, because the placer stops optimizing when it thinks timing is met. Later, when the design genuinely didn't meet at 3.4 ns, the same relaxation helped and the placer found a shorter path. The rule isn't "tighter is better", it's that the constraint should sit close to the achievable limit.
- **The critical path moves between synthesis and routing.** Post-synth it's the book's rescan comparison, post-route it's the L3 write port. Placement decides which path is worst, so you can't identify the real bottleneck before routing.
- **Verilator-clean isn't Vivado-clean.** A design that linted, built, and passed every test failed at Vivado elaboration, twice, for reasons simulation has no concept of.

## Toolchain

SystemVerilog · Verilator 5.050 · C++ · Vivado 2024.1 · ZCU104 (xczu7ev-ffvc1156-2-e)