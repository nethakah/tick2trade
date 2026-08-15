# Build Log (since August 2026 - RTL begun)

---

### log-1: FSM (2026-08-03)
- From what I've considered, I could do the parser in 2 different ways: 1. FSM, and 2. a speculative approach.
- Originally, I assumed the FSM (finite-state-machine) approach, which uses 1 shared decoder. Here, a state reg tracks whether the incoming byte fed is a type code or a body byte, a counter tracks position in the message, and some routing sends the byte to the right field. This is an attempt to be cheap in area and allow latency to be ~1 cycle after the final byte.
- I discovered the speculative approach in an online paper by Ruixuan Zhang [https://doi.org/10.36227/techrxiv.174803766.68744651/v1], where it's almost the opposite approach. Here, there's an instantiated dedicated decoder PER message type which all run in parallel from the very "0th" byte. This decoder checks the byte against its own type and only the matching decoder keeps going. The claim is 1-cycle latency without ANY state-based routing delay as proposed in my approach.
- Once the pipeline works end-to-end, I'm hopeful to return to this speculative approach instead and implement it into the same AXI stream I've built, then measure both on the ZCU104 board since Zhang's paper doesn't publish any comparative area figures against an FSM baseline. Then through 2 parser microarchitectures, I can quantify the area-latency tradeoff.

### log-2: ITCH 5.0 Spec (2026-08-03)
- Every single byte offset and field length in the RTL MUST trace to a table in NASDAQ's official specification 5.0 [https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf]. 
- There's quite a few useful architectural resources that base their information on a 4.x version, like a Columbia 2013 paper I found [https://www.cs.columbia.edu/~sedwards/classes/2013/4840/reports/Itch.pdf]. For specificity, the 4.x tables have different offsets so modeling those would be wrong today.

### log-3: B2B (2026-08-03)
- Exchange feeds don't pause; the final byte of a message is followed by the type byte of the next. 
- If the FSM needs an idle cycle to reset its counter or asserts a valid too late, we won't catch it in a testbench that simulates with gaps once a real DMA (Direct-Memory-Access engine) feeds it continuously.
- Thus, I've decided for the FSM to jump directly from the final body byte right back to the `READ_TYPE` state with no sort of IDLE state that we typically see in RTL. 

### log-4: The Xilinx ZCU104 (2026-08-03)
- All thanks to my research lab at CMU, I'll be accessing a ZCU104, which carries a XCZU7EV-2FFVC1156 Zynq UltraScale+ MPSoC, which contains roughly 230K LUTs, 11 Mb of BRAM, 27 Mb of UltraRAM, on a 16nm process at speed grade -2, from what I discovered online.
- Looking at similar-topic projects repos, I've seen an xc7z020, which this is already ~4x the logic, ~8x the on-chip memory, and a whole process generation faster.
- Due to this opportunity, I'm changing the software baseline. The PS (Processing System - the real ARM CPU hardened into the chip: 4 Cortex-A53 cores at ~1.2 GHz) vs PL (Programmable Logic) comparison will be against the chip's 1.2 GHz A53.

### log-5: Verilator/C++ (2026-08-03)
- I've chosen to write testbenches in C++ against Verilator models, instead of the easier cocotb (Python), despite only knowing C!
- Now it's not conventional simulation, it's compilation that essentially translates synthesizeable SV into a C++ class. I'm going for an ordinary C++ program that will set inputs, toggle the clock, and assert on outputs.
- Why? Well, it's fast, and C++ verification is far more popular in HFT firms. Besides that, Verilator's linter feels considerably stricter to read my code and report problems in the text.
- A caveat: Verilator is 2-state (using (0,1) instead of (0,1,X,Z)), so unitialized register bugs don't surface like in Icarus Verilog since there's a forced 0 or 1 instead of X warnings. 

### log-6: L3 vs L2 Memory (2026-08-03)
- For reference: L3 = Level 3 = every indivudal order tracked separately by its order reference number; L2 = Level 2 = orders aggregated by price level with their identities discarded.
- This order book has 2 memory needs. The L3 book maps order reference numbers to order details, which is super memory hungry. The L2 ladder is way smaller.
- Thanks to the ZCU104, UltraRAM (a UltraScale+ primitive), we get 288 Kb blocks, which is roughly ~8x denser than a 36 Kb BRAM, and natively 72 bits wide by 4096 deep. Hence, the L3 table can hold hundreds of THOUSANDS of live order entirely on-chip.
- Without UltraScale+ silicon, I'd pull back to BRAM on 7-series parts likely, but luckily, I'll be using URAM for the large table and BRAM for the small one, rejecting BRAM-only!

### log-7: DMA Ingress (2026-08-03)
- The current scope feeds the pipeline from DDR (the 2GB of DRAM hardened next to the chip attached to the PS side) through a PS to PL (from processor side to fabric side) AXI-DMA engine. In other words, software running on the A53s will load a file of ITCH messages into memory, then the DMA engine streams that memory out on its own as an AXI4-Stream with no CPU involvement per byte.
- Thus, the path is: ITCH file -> software -> DDR4 -> AXI-DMA -> AXI-Stream -> my parser -> my order book -> signal.
- Key thing to note is that backpressure is lossless here because if the parser deasserts `tready`, the DMA pauses so data sits in DRAM with nothing dropped. 

### log-8: Packaging for AXI4-Stream Data (2026-08-03)
- The parser's output payload is a packed struct on `tdata` instead of separate ports, even though multiple fields works fine, since an IP that is actively expecting a SINGLE payload bus makes things a bit unideal.

### log-9: Message Length Derivations (2026-08-03)
- MoldUDP64 (as referenced by the NASDAQ specification) prefixes ITCH messages with a 2-byte length field. Hence, the deframer technically hand the parser the length, and then we don't really need the lookup table. But I'm opting to derive the length myself.
- Reason 1: if my parser doesn't depend on framing data, it'll work smoother with any source (testbench bytes, raw replays, deframer, no deframer, etc.).
- Reason 2: Cross checking always helps!
- Since msg_length() as written is a combinational lookup completed in parallel and in the same cycle the type byte arrives, so there's no latency overhead here. A comparator would cost some area and I'd have to figure out what to do on mismatch, but either way the latency (in cycles) is fine.

### log-10: Wire Format (2026-08-03)
- The parser converts on-wire encodings to internal representations, and the raw protocol format disappears downstream. More specifically, the ASCII message type byte is a 4-bit enum, the Buy/Sell is a single bit. So all protocol knowledge is designed to live in ONE module, maybe this will be more valuable on a speculation of revised type codes or feeding the same orderbook a different exchange protocol.
- Although, price is NOT decoded (still raw) and the tracking number is dropped entirely (NASDAQ's spec says its Nasdaq-internal so its a dead wire to worry about).

### log-11: Deconstructing Messages (2026-08-03)
- I'm keeping explicit (~60) case arms, one per byte to route that byte into a specific part of a specific field. It's verbose and I could collapse it in the future (shift incoming bytes into a buffer and extract fields once message completes, use macros to generate repetitive arms, generate RTL from machine-readable spec descriptions, etc.), BUT I want every line to map to exactly ONE row of a spec table.
- Any later optimization should be checked against this explicit version which is verifiable by inspection. We'll go for correctness first, and compress later. 

### log-12: Sync Active LO Reset (2026-08-03)
- Registers reset syncrhonously on clock edge, using the active-low version, `rst_n`. It's recommended on UltraScale devices for arhitectural purposes: the flip-flop primitives have a dedicated sync reset input, so using a sync reset costs no extra logic while a typically async reset would have to route differently.
- And a second choice is part of this, we only reset signals that must be deterministic. Namely: FSM state, byte counter, output valid. I've opted NOT to reset the ~325 (now ~384 with word alignment by 2026-08-05) payload flip-flops, leaving it more to a "who cares until they're written" philosophy, since resetting them costs area and timing for absolutely no purpose.
- In the case of ASIC though, the ASIC libraries would prefer async assert with sync deassert since reset must take effect before the clock is running. I currently have sights to test my parser in my research lab via Cadence flows (to get a PPA measurement (Power, Performance, Area)), where Genus might honestly object to this design choice. 

### log-13: FIFO Async (2026-08-04)
- The 2 clocks meet at the pipeline's ingress (AXI-DMA delivers at PS's AXI-HP inferface clock but the core will run at something faster).
- It makes sense to not limit a latency-based project to that slower domain's clock, and tick-to-signal is going to be measured at cycles*clk_period in nanoseconds. CDC gets me that result much better.
- We cant wire 2 domains directly since things WILL become metastable in enough time. 

### log-14: FIFO Architecture (2026-08-05)
- Something I realized is that the actual data never crosses dangerously.
- In particular, writing stores a byte into `mem[slot]` at the w_clk edge, and that byte obviously doesn't change for many cycles. Only the pointers cross, so I really just needed to worry about the `empty` and `full` flags so the pointers know whether there's things to read/write respectively.
- The pointers here are multi-bit, which is the problem since that allows for metastability where something like 01111 to 10000 can give us 11111 which isn't a value we ever actually held.
- I implemented the standard gray-code fix (consecutive values differ in exactly 1 bit) so we'll resolve uncertain bits to the old or new one which are real positions for the pointer.
- I was confused how this is solid but the key inisght is the FIFO pointer moves by ONE per cycle always, so a pointer cannot jump by 3 for example and break the guarantee given by gray code.
- Hence stale w_ptr in read domain makes FIFO look emptier than it is, so reader waits, and a stale r_ptr in write domain similarly. So we NEVER read data that's not there or overwrite data, the worst it gets is a wasted cycle.
- I included a ptr_gray for the crossing (for safe encoding) and also a synchronized ptr_Gray copy on the far side since the signal from that "other" clock isn't usable until we pass thru 2 flops so we give metastability a full clock to decay.
- I made the pointers $clog2(DEPTH)+1 with that extra bit so we have a "lap counter" which allows us to understand full/empty properly. (From what I understand as of now, this is also why we need DEPTH to be a power of 2).

### log-15: FIFO Not Converting Gray to Binary (2026-08-05)
- One way we could understand the `full` flag is by converting the scrambled gray back to binary (so binary[i] = XOR of all gray bits at i and above i).
- Instead, since `gray = binary ^ (binary >> 1)`, we can gather that the top bit is flipped directly and the bit below it as well, while every lower bit is untouched. So we just need to worry about the top 2 bits and compute an equality with it.
- I picked this for SPEED. It's more elegant to write it out and use a conversion function, but an XOR chain like that that grows with ptr width is taxing when we are measuring in nanoseconds of latency.

### log-16: FIFO Async Read on r_data for FWFT (2026-08-05)
- I opted to do the memory read combinationally, which infers LUTRAM instead of BRAM.
- The AXI-Stream contract wires !empty to s_axis_tvalid and r_data to s_axis_tdata. Since we need tdata valid and tvalid high, with a registered read we know empty will deassert 1 cycle before r_data, so we cannot assert tvalid over stale old data - it's wrong.
- This async read gives FWFT (First-Word Fall-Through) so the head is always present on the output when the FIFO is NONEMPTY, so the 2 signals are consistent with no other logic.
- Note here the cost of LUTs instead of a BRAM block. But thanks to the board I'm getting access to, this is quite negligible.

### log-17: FIFO Shallowness (2026-08-05)
- DEPTH=16 right now and in this scope it doesn't need to grow.
- CDC FIFO needs enough depth to cover synchronizer latency (ptr takes 2-3 cycles to become visible across boundary so flags can be stale briefly)
- Deep FIFOs have to absorb bursts which I just don't need here. Since we're having DMA ingress, backpressure is lossless (log-7), so when FIFO fills then full asserts and DMA pauses and data waits in DDR4 with NOTHING dropped. 
- Something tangentially related and interesting about deep buffering through, you'd think a real live feed needs it since it doesn't pause, but in HFT having stale messages is catostrophic in its own respect. Hence its actually preferable to drop and resync, and hopefully just have a pipeline fast enough to not need buffering, using gap detection for problems.

### log-18: Skid Buffer for Parser Output (2026-08-05)
- AXI4-Stream needs smth to hold tvalid with stable tdata until consumer asserts tready.
- The parser cant freeze while bytes keep arriving, so we'll park the message in the skid buffer while the book stalls.
- The skid buffer has 2 slots and s_tready comes from a registered state (not combinationally from m_tready).
- This should just cost me 1 cycle of latency, which is usually a net win since latency = cycles × period and breaking the chain raises the achievable clock.
- W/o the skid buffer here, m_axis_tready from the order book feeds combinationally back into the parser's s_axis_tready — and in a long pipeline those combinational ready-paths chain together across every stage, and that path gets long until it lands on the critical path, and then Fmax drops. A skid buffer registers the ready signal, breaking the chain. 

### log-19: Backpressure Chain Closure (2026-08-05)
- Wrote `s_axis_tready = !(fsm_tvalid && !fsm_tready)` into the parser.
- This makes the parser stop consuming bytes when it's holding output which the skid buffer cannot current take in.
- skid fills --> parser stops consuming --> FIFO fills --> DMA pauses = lossless directly from DDR4 (msgs stay in RAM untouched until we're actually ready to read them so nothing is lost)

### log-20: Padded msg_t to 384 bits (2026-08-05)
- Updated from 325 bits to 384 bits because I want every field to start on a 32-bit boundary.
- Since we're using Verilator, and any signal wider than 64 bits is an array of uint32_t words. Without this padding, things that span multiple words would need a shift and stitch type of code mechanism, which is just messy.
- With it padded, every field is 1 whole word or 2, so extraction is at MOST a shift+mask.
- This does cost me ~59 flip flops with no latency impact, so that's fine given the hardware I have access to here, and its standard for structs crossing hardware/software boundary (like how C compilers pad structs).
- Also had to group sub-byte fields since having 1-bit fields between multi-byte fields misaligns things.

### log-21: Skid Buffer Latency Measured (2026-08-06)
- I completed a test end-to-end. 36 bytes in, `m_axis_tvalid` asserts 1 cycle after the final byte, holds for exactly 1 cycle, then drops. This means the buffer latches and drains correctly, so the predictive cost in log-18 is now confiremd!

### log-22: The testbench polling for conditions (2026-08-06)
- Rather than counting cycles (I got worried hardcoding tick "x" times would break once pipeline depth changes), I wrote a `wait_for_msg()` function that ticks until `m_axis_tvalid` asserts (with a timeout of course), and then returns the number of cycles waited. 
- Small architectural thing: we read expectations from the same struct that generated input bytes so no copies to worry about in terms of sync-ing.

### log-23: Verified the generator before the RTL (2026-08-06)
- I took the message builder for the TB, hex-dumped it, and then checked byte-by-byte against ITCH spec tables before using it on the parser.
- I thought this was a good idea that might come in handy again, since if we are measuring using this "generator," it's crucial we calibrate it first or it throws everything off. Hence the generator will not cause any bugs in it of itself!

### log-24: Verifying the parser randomized (2026-08-10)
- 100,000 messages passed with random fiedl values, streamed b2b, and checked in every field against pre-determined expectations at generation time of those messages.
- It's a regression, not a stress test, since the seed is fixed and printed on failure so we can reproduce them, and every msg is checked rather than counted.

### log-25: Contracts for testbench (2026-08-13)
- Thanks to 15-122 this summer I felt obligated to make the C++ code proveably correct (or as much as I could). So I added REQUIRES/ENSURES (using assert()) so I can check preconditions and postconditions.

### log-26: Parser verified!!! (2026-08-13)
- Six tests passed A,E,D individually, back-to-back A to E to D, backpressure with mid-stream stalling, and finally 100,000 randomized tests with mixed-type random fields!
- Found 2 RTL bugs (bug-05, bug-06) which were AXI violations on each port.
- Takeaways!: Need to make sure both sides of AXI interface respect the handshake, can't have the producer or consumer end up "lying" to one another.
- Parser is DONE; I've decided to postpone F/C/X/U message types for now and just maintain A/E/D to build the working book so we can get to real numbers on the ZCU104. Wrap back around to finish the other message types afterwards.

### log-27: Async FIFO verified (2026-08-14)
- Tested: reset, 1-word crossing, fill/drain, 200 W/R pairs to wrap the wheel 12 times.
- No tick() here, used 2 clocks with coprime periods which increment by step() function so the edges go relative to each other.
- Measured synchronizer latency, `empty` deasserts around 24 time units (around 2 r_clk periods) post-write. So that's the 2-flop crossing cost.
- Small bug found in TB: I checked write being refused when full straight after the 16th write, but `full` was computedin the write domain from the sync'd read pointer so there's lag in the fill so the FIFO accepted the write. Instead I let it run until `full` asserts before checking refusal.
- I wanted to note that CDC flags are conservative but stale, meaning any logic that drives this FIFO has to respect the lagging of the flags by a couple of cycles. If you assume they update instantly, the FIFO gets driven into states it shouldn't be at.
