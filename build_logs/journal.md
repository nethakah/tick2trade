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

### log-28: FIFO coverage gap (2026-08-15)
- For the async FIFO testbench, the 4 tests use SEQUENTIAL DATA and a single clock ratio (20/13 but adjustable to other coprime values).
- Untested still: randomized payloads; other clock ratios / drift patterns.
- Should be fine for now since the FIFO is agnostic with regards to data (we test moving 1-16 for any values so any structural bugs should be caught already). 
- Notes for me: revisit this if there are FIFO-esque bugs (no idea how this will look but we shall see) and update tests to randomize parameters afterwards.

### log-29: MoldUDP64 deframer verified (2026-08-15)
- 2 tests: 1 packet with 2 msg; 2 packets with a sequence jump for gap detecting.
- Just for reference, the deframer exists since the parser assumes byte 0 is a message type, even though the real data arrives formatted like: `[Session(10B)][Sequence(8B)][Count(2B)][Len(2B)][ITCH msg][Len(2B)][ITCH msg]...`. So we can't feed raw packets to the parser straight away, we need the deframer to strip the 20B header and all the 2B length prefixes.
- Also used a shift reg for every multi-byte field to keep the low bits then append the new byte. Then after N bytes the first byte received will walk to the top which is big-endian conversion easy and free.
- `s_axis_tlast` used for end-of-packet checking. Technically the FSM already knows the packet ends from the msg_count but this is a free check to make sure no packets were truncated or malformed along the way. It's redundant but free kind of like I did in log-9 cross-checking.

### log-30: Orderbook msg_pkg.sv (2026-08-15)
- The Columbia paper I referenced earlier in the logs happens to use an AVL for the L3 in order to get O(log n) with rebalance on insert. However, in the HFT context, this is not deterministic since the tree depth depends on how many orders are live meaning it is slow when the book is deep (which is precisely when it matters for us to not fail). 
- First thought was to use capped linear probing but it's actually bad too since dropping Adds will mess up later Executes entirely.
- Eventually got the right answer which is from existing FPGA hash-table literature, which is to widen the memory. We'll hold X entries in one word, read in a single access with all keys compared in parallel. Hence we're only taxed 1 memory read, 4 comparators (if we cap at 4), and get constant latency with no probing at all.
- Overflow will be handled by sizing instead of dropping orders since 27Mb of UltraRAM gives us 16,000+ live orders along with tracking an overflow counter as a status reg to make sure it cannot happen.
- Still deterministic, NOT average. 

### log-31: Order book structure (2026-08-15)
- L3 (book memory) holds all orders by reference number.
- L3 existence rationale: E/D only have a ref_num and no price/signal, so we have to lookup to get that information and apply changes to a price level.
- L2 (bid and ask levels) holds shares aggregated by price (each price has a level).
- L2 existence rationale: finding best bid by scanning every order on every message is not feasible, instead we can hash with parallel compare for constant time lookup.

### log-32: Top-of-book max/min (2026-08-16)
- Bids win by being higher, so top_bid_price resets at 0.
- Asks win by being lower, so top_ask_price resets at 0xFFFFFFFF (for 32b).

### log-33: Rescanning case (2026-08-16)
- Adding improves top of book only, so it's 1 comparison.
- We only track at the top, but when the level at the top of book is emptied then we know the new best will be the highest remaining bid but we don't track that anywhere (its at a hashed position so we can't just go "one level down" or anything like that), so we'd have to do a bounded scan of entries.
- Obviously tracking 2nd best means we track 3rd best etc. etc. so we can't do that.
- I considered a sorted structure but then adding is O(n) so that's not possible. So as of now I see no other architectural option than to accept that when a top level fully empties is the only time we are not working in constant O(1) time. And it's always 256 cycles so it's not THAT horrible. I'll update if I find a workaround that is better.

### log-34: Hash width (2026-08-17)
- Originally I narrowed the XOR fold to BOOK_ADDR_WIDTH but it caused a linting problem where 16 bits weren't utilized at all.
- Instead fold at full 16b width and then afterwards truncate to the address width so we use every bit to generate the result.

### log-35: Order book verification (2026-08-17)
- Verified with 11 tests: check reset baselines the price, check add will set top of book if it should, check add cannot worsen the top of book, check adding at the same price aggregates to one L2 level, check executing part of a level reduces shares only, check full execution of a level empties it and runs a rescan if top of book, check executing on unknown reference counts as a miss, check delete removes the order and rescans if top of book, check delete does not remove the level if there's remaining orders, check delete on an unknown reference counts as a miss, check the whole lifecycle.
- 2 bugs found (bug-07 and bug-08) about ask-side rescan not firing and testbench state leaking between states because memory is not reset in the RTL.
- Structured these tests by message type (ADD/EXC/DEL).
- Skipped randomized testing for now for the book, unlike the parser. Since with thousands of random cases I'd need to make a C++ reference model/program to check. Going to move onto the signal engine and then come back to this instead!

### log-38: trade_signal outline (2026-08-18)
- Looked into how HFT firms split work between hardware and software, since my instinct was that there's no way their FPGA teams configure actual strategies into fabric.
- It seems, although the industry is very convulated and mysterious, it does seem that it follows the obvious pattern of software deciding/encoding algorithms and strategies while hardware decides when to fire things. So I modeled this module after that: cfg_* are preloaded order details, which would be configured by the software via AXI-Lite (encoding things into the memory directly so we can access it in the hardware easily).
- The in-fabric strategy here is just comparators that allow those preloaded values to work as fast as possible. I could be wrong in terms of scope but it was the best I could come up with in terms of getting close to HFT infra.
- Some things are also gates for pre-trade risk checks, like cfg_size_min for preventing slippage (only firing if we have enough shares to be filled completely), and cfg_spread_max to not trade in a market with an overly large spread, and cfg_armed as a killswitch to stop trading immediately.

### log-39: book_valid gate (2026-08-18)
- For firing in `trade_signal.sv`, I made the condition `if (book_valid && conditions_ok)`. You could just check the various conditions, but that would emit an order on every single clock cycle, which would be hundreds of millions per second at something like 300 MHz. 
- Instead, `book_valid` pulses once per market change so we do one order per market event and edge-triggered.

### log-40: SVA on modules (2026-08-18)
- Once again, thanks to 15-122 at CMU, I decided to incorporate something similar to contracts into the RTL too using `assume` and `assert` statements.
- Syntax was a bit new to me: `x |-> y` means if x then y on same cycle; `x |=> y` means if x then y on next cycle; `$past(x)` means x one cycle ago; `$stable(x)` means x unchanged since last cycle; `disable iff (!rst_n)` because we don't check during reset (`!` because active HI still remember).
- Using this in an `ifdef` type thing means we need `--assert +define+SIM` on the verilator build command to check them and include them respectively. 
- I believe some of these assertions would've caught bug-04, bug-06, and bug-07, and maybe more with some analysis!
- Did have a few hiccups with the assertions - make sure you write them right or you're going to be wondering whether the testbench or RTL or assertion is wrong, which was not fun (bug-09 but it happened far more times than I logged it).

### log-41: Complete pipeline integrated and verified (2026-08-19)
- `tick2trade_top.sv` connects everything together finally, barely any logic here of course mostly just wiring.
- Did 5 integration tests, which all passed.
- Did both clock domains in the same testbench (instead of a single tick() we conditionally tick the relevant clock).
- Widened ingress FIFO to 9 bits to include tlast too.
- FIFO to AXI-Stream is 4 lines and works perfectly thanks to log-16 (r_data is already valid when !empty  which is AXI's requirement precisely).

### log-41: Prepping for board (2026-08-19)
- Starting with loose 10ns constraint
- Plan: synthesize, read WNS (worst negative slack), compute = target - WNS, tighten, repeat until WNS approaches 0.
- DMA clock is configurable via PS in Zynq block design so I won't set it beforehand since we don't know what the cores might achieve.

### log-42: Synthesis failures (2026-08-19)
- book_mem at BOOK_ADDR_WIDTH=12 gives us 4096*4*160 = 2621440 bits. The elboration limit in Vivado 2024.2 for a single varaible is 1mil. 
- Big thing here is that it's actually not a capacity problem, since 2.6Mb against the 27Mb of URAM is fine, but it's a limit on how big a variable the tool can model before deciding what becomes RAM.
- Originally chose 12 with operational reasoning (related to log-30), but didn't take into account future tool limits. 
- Dropped this to BOOK_ADDR_WIDTH=10, so 1024*4 = 4096 live orders, which is under the limit. 
- The better fix would be to restructure the packed bucket into an unpacked outer dimension so we can have the full size originally scoped.

### log-43: Struct arrays won't infer as RAM (2026-08-20)
- Vivado built the L3 table of FFs and muxes which made synthesis so long.
- Tried `ram_style = "block"` but it was ignored due to "incorrect usage", which was the result of decomposing the packed array into per-field objects instead of memory words.
- Fixed it by making flat bit vector `logic[BUCKET_BITS-1:0] book_mem[NUM_BUCKETS]` and casting at the boundary `bucket_t'()`. The field access in the FSM is the same but the declaration just changed a bit to make Vivado agree. 
- Also realized I can't field-select off a cast (i.e. writing `level_t'(x).valid`).

### log-44: BRAM and URAM are 72b wide (2026-08-20)
- Inference worked from log-43 fix but BRAM still refused by Vivado.
- The bucket is 640 bits as is at this point, yet BRAM and URAM are natively 72 bits per port so there's a mismatch here.
- Unfortunately not fixable without changing the infra a lot here - 640b width is literally the design I picked since I want all 4 entries read in one access and compared in parallel, while narrowing it to fit would mean 4 sequential reads per lookup.
- Hence, log-6 is wrong. I was reasoning about capacity when the constraint now is port width, so I'm just accepting LUTRAM (especially since it's only 8.7% of the device), and after trying to research a bit more into this convulated space I do think real HFT matches that anyway - using LUTRAM for wide single-cycle access and BRAM for bulk state which we don't really have to worry about.

### log-45: L2 ladder collision feedback (2026-08-20)
- L3 buckets hold 4 entries so hash collisions have 3 fallbacks before things are lost.
- L2 has 1 slot per hash index so if a price hashes to a slot holding a different price then we never create that level and it's only tracked by level_collision_count.
- I didn't notice the asymmetry here, that 1 collision in L2 loses a price level while 4 before L3 loses an order.
- Mitigated this by sizing, moving LEVEL_ADDR_WIDTH to 10 instead of 8 so we now have 1024 slots, so a few dozen live levels occupy ~4% and collisions are even less likely.

### log-46: Symbol filtering (2026-08-20)
- We carreid stock_locate but never read it really.
- Two symbols (e.g. AAPL and MSFT) could have orders sharing the same hash table and ladders.
- To fix this I added cfg_stock_locate as input and made it so IDLE state only accepts matching messages. Hence, the order book is officially built to manage ONE ticker/company. Costed only 20 more LUTs.
- For multi-symbol structure, we'd need to index every part of this by stock_locate, and even at 100 symbols thats already 64Mb. In real production or if I had more budget or something, these orderbooks would be across devices/FPGAs.

### log-47: Timing out-of-context synthesis (2026-08-20)
- `report_timing_summary` via tcl scripts returned an empty table! `check_timing` explained it though, Vivado won't summarize a design that is mostly uncosntrained I/O which was inherent here since every port connects to other logic rather than a pin for now. Just use `report_timing` for synthesizing.
- Also needed to have HD.CLK_SRC since OOC has no clocker buffer so no clock delay model to look at to make timings make sense.
- Small thing that messed with me was that `set_property` with an invalid site name fails but SILENTLY. Make sure you check the correct real sites from `get_sites -filter {SITE_TYPE =~ *BUFG*}`.

### log-48: 294MHz post-route (2026-08-20)
- 10ns (+7.377) to 3ns post-synth met (..) to 3ns post_route violated (-0.336) to 3.4ns post-route met (+0.012).
- Post-synth said 381MHz and post-route said 294MHz. Post-route is the honest number.
- Critical path moved between stages.
- Final: 19914 LUTs (8.64%), 2615 FF (0.57%), 0 BRAM/URAM/DSP.

### log-49: Relaxing constraint made timing worse (2026-08-20)
- 3.4ns: met +0.012, path 3.300ns, 10 levels, 68% routing.
- 3.5ns: violated -0.102, path 3.414ns, 3 levels, 93% routing.
- I think a looser target makes the placer stop optimizing early so cells land further apart in the end. Went back to 3.4ns in the constraint.