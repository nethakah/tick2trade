# Tick to Trade

A NASDAQ ITCH 5.0 market data pipeline in SystemVerilog, targeting a Xilinx ZCU104 (Zynq UltraScale+ XCZU7EV-2). Raw MoldUDP64 packets in, order book state and a trading signal out, with deterministic nanosecond latency.

> In progress. Verified modules are marked below; the order book and signal engine are next.

## Architecture

```
DMA ──▶ async_fifo ──▶ moldudp_deframer ──▶ itch_parser ──▶ order_book ──▶ signal
        (CDC)          (strip envelope,     (decode into    (L3 + L2)
                        detect gaps)         msg_t struct)
```

Every stage boundary is AXI4-Stream, so the ingress source is swappable — a 10G Ethernet MAC emits the same interface the DMA does.

| Module | Purpose | Status |
| :-- | :-- | :-- |
| `msg_pkg.sv` | Shared types: message enum, packed `msg_t`, length/decode lookups | done |
| `async_fifo.sv` | Dual-clock FIFO, Gray-coded pointers, two-flop synchronizers | verified |
| `skid_buffer.sv` | Generic 2-slot AXI4-Stream register slice | verified in-context |
| `moldudp_deframer.sv` | Strips MoldUDP64 framing, detects sequence gaps | verified |
| `itch_parser.sv` | Decodes Add / Executed / Delete into a parallel field bundle | verified |
| `order_book.sv` | L3 order table (UltraRAM) + L2 price ladder (BRAM) | verified (unrandomized) |
| `signal.sv` | Top-of-book, spread, imbalance | in progress |
| `tick2trade_top.sv` | Integration | planned |

## Why two parsing stages

Market data arrives as UDP packets, not a clean byte stream:

```
[Session 10B][Sequence 8B][Count 2B][Len 2B][ITCH msg][Len 2B][ITCH msg]...
```

The **deframer** strips that envelope and emits bare ITCH bytes. The **parser** decodes each message into fields. They're separate because MoldUDP64 packs several variable-length messages per packet — you can't locate message boundaries without first walking the length prefixes.

The deframer also tracks sequence numbers. UDP drops packets, and MoldUDP64 sequence numbers are monotonic across packets, so `seq != prev_seq + prev_count` means messages were lost. That's the only way a ticker plant knows its book might be wrong.

## Verification

Verilator with C++ testbenches. Every module is self-checking; tests assert and return nonzero on failure.

```bash
./scripts/lint.sh                     # lint gate, run before every commit

rm -rf obj_dir && verilator --cc --exe --build -j 0 \
    --top-module itch_parser \
    rtl/msg_pkg.sv rtl/skid_buffer.sv rtl/itch_parser.sv \
    tb/parser/tb_itch_parser.cpp && ./obj_dir/Vitch_parser
```

| Testbench | Coverage |
| :-- | :-- |
| `tb/parser/` | A/E/D individually, back-to-back stream, mid-stream backpressure, 100,000 randomized mixed-type messages |
| `tb/fifo/` | Reset, single-word crossing, fill-to-capacity + drain, 200-cycle pointer wraparound across two coprime clock domains |
| `tb/deframer/` | Single packet with multiple messages, sequence-gap detection |

Testbenches use 15-122 style `REQUIRES`/`ENSURES` contracts over `assert()`. Stimulus is back-to-back by default — real feeds and DMA never pause between messages, and gapped stimulus hides last-byte/first-byte FSM bugs that otherwise only appear on hardware.

## Design notes

Every byte offset in the RTL traces to a table in the official Nasdaq TotalView-ITCH 5.0 specification. Two engineering decisions worth calling out:

**Wire format is decoded at the parser boundary.** The ASCII message type becomes a 4-bit enum, the Buy/Sell indicator becomes one bit. No downstream module knows NASDAQ chose letters, so a protocol revision touches only the parser. Price stays raw `Price(4)` fixed-point — integer comparison preserves ordering, so the hardware never divides.

**Backpressure is lossless end to end.** Ingress is DMA from DDR4, so when the book stalls the pressure propagates back through the skid buffer, the parser, and the FIFO until the DMA pauses and data simply waits in memory. A live exchange feed cannot be backpressured and would instead require drop-and-recover using the sequence numbers the deframer already tracks.

`docs/log.md` records every non-obvious decision and its rationale. `docs/bugs.md` records every non-trivial bug: symptom, cause, fix, and what class of mistake it belonged to.

## Toolchain

SystemVerilog · Verilator 5.050 · C++ · Vivado (synthesis, timing) · ZCU104