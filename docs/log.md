### L-X: Title (Date Written)
- Notes on the topic.

---

### L-1: FSM (08.03.26)
- From what I've considered, I could do the parser in 2 different ways: 1. FSM, and 2. a speculative approach.
- Originally, I assumed the FSM (finite-state-machine) approach, which uses 1 shared decoder. Here, a state reg tracks whether the incoming byte fed is a type code or a body byte, a counter tracks position in the message, and some routing sends the byte to the right field. This is an attempt to be cheap in area and allow latency to be ~1 cycle after the final byte.
- I discovered the speculative approach in an online paper by Ruixuan Zhang [https://doi.org/10.36227/techrxiv.174803766.68744651/v1], where it's almost the opposite approach. Here, there's an instantiated dedicated decoder PER message type which all run in parallel from the very "0th" byte. This decoder checks the byte against its own type and only the matching decoder keeps going. The claim is 1-cycle latency without ANY state-based routing delay as proposed in my approach.
- Once the pipeline works end-to-end, I'm hopeful to return to this speculative approach instead and implement it into the same AXI stream I've built, then measure both on the ZCU104 board since Zhang's paper doesn't publish any comparative area figures against an FSM baseline. Then through 2 parser microarchitectures, I can quantify the area-latency tradeoff.

### L-2: ITCH 5.0 Spec (08.03.26)
- Every single byte offset and field length in the RTL MUST trace to a table in NASDAQ's official specification 5.0 [https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf]. 
- There's quite a few useful architectural resources that base their information on a 4.x version, like a Columbia 2013 paper I found [https://www.cs.columbia.edu/~sedwards/classes/2013/4840/reports/Itch.pdf]. For specificity, the 4.x tables have different offsets so modeling those would be wrong today.

### L-3: B2B (08.03.26)
- Exchange feeds don't pause; the final byte of a message is followed by the type byte of the next. 
- If the FSM needs an idle cycle to reset its counter or asserts a valid too late, we won't catch it in a testbench that simulates with gaps once a real DMA (Direct-Memory-Access engine) feeds it continuously.
- Thus, I've decided for the FSM to jump directly from the final body byte right back to the `READ_TYPE` state with no sort of IDLE state that we typically see in RTL. 

### L-4: The Xilinx ZCU104 (08.03.26)
- All thanks to my research lab at CMU, I'll be accessing a ZCU104, which carries a XCZU7EV-2FFVC1156 Zynq UltraScale+ MPSoC, which contains roughly 230K LUTs, 11 Mb of BRAM, 27 Mb of UltraRAM, on a 16nm process at speed grade -2, from what I discovered online.
- Looking at similar-topic projects repos, I've seen an xc7z020, which this is already ~4x the logic, ~8x the on-chip memory, and a whole process generation faster.
- Due to this opportunity, I'm changing the software baseline. The PS (Processing System - the real ARM CPU hardened into the chip: 4 Cortex-A53 cores at ~1.2 GHz) vs PL (Programmable Logic) comparison will be against the chip's 1.2 GHz A53.

### L-5: Verilator/C++ (08.03.26)
- I've chosen to write testbenches in C++ against Verilator models, instead of the easier cocotb (Python), despite only knowing C!
- Now it's not conventional simulation, it's compilation that essentially translates synthesizeable SV into a C++ class. I'm going for an ordinary C++ program that will set inputs, toggle the clock, and assert on outputs.
- Why? Well, it's fast, and C++ verification is far more popular in HFT firms. Besides that, Verilator's linter feels considerably stricter to read my code and report problems in the text.
- A caveat: Verilator is 2-state (using (0,1) instead of (0,1,X,Z)), so unitialized register bugs don't surface like in Icarus Verilog since there's a forced 0 or 1 instead of X warnings. 

### L-6 L3 vs L2 Memory (08.03.26)
- For reference: L3 = Level 3 = every indivudal order tracked separately by its order reference number; L2 = Level 2 = orders aggregated by price level with their identities discarded.
- This order book has 2 memory needs. The L3 book maps order reference numbers to order details, which is super memory hungry. The L2 ladder is way smaller.
- Thanks to the ZCU104, UltraRAM (a UltraScale+ primitive), we get 288 Kb blocks, which is roughly ~8x denser than a 36 Kb BRAM, and natively 72 bits wide by 4096 deep. Hence, the L3 table can hold hundreds of THOUSANDS of live order entirely on-chip.
- Without UltraScale+ silicon, I'd pull back to BRAM on 7-series parts likely, but luckily, I'll be using URAM for the large table and BRAM for the small one, rejecting BRAM-only!

### L-7 DMA Ingress (08.03.26)
- The current scope feeds the pipeline from DDR (the 2GB of DRAM hardened next to the chip attached to the PS side) through a PS to PL (from processor side to fabric side) AXI-DMA engine. In other words, software running on the A53s will load a file of ITCH messages into memory, then the DMA engine streams that memory out on its own as an AXI4-Stream with no CPU involvement per byte.
- Thus, the path is: ITCH file -> software -> DDR4 -> AXI-DMA -> AXI-Stream -> my parser -> my order book -> signal.
- Key thing to note is that backpressure is lossless here because if the parser deasserts `tready`, the DMA pauses so data sits in DRAM with nothing dropped. We don't need any drop-and-recover or gap counters! It's simple and correct as is.

### L-8: Packaging for AXI4-Stream Data (08.03.26)
- The parser's output payload is a packed struct on `tdata` instead of separate ports, even though multiple fields works fine, since an IP that is actively expecting a SINGLE payload bus makes things a bit unideal.

### L-9: Message Length Derivations (08.03.26)
- MoldUDP64 (as referenced by the NASDAQ specification) prefixes ITCH messages with a 2-byte length field. Hence, the deframer technically hand the parser the length, and then we don't really need the lookup table. But I'm opting to derive the length myself.
- Reason 1: if my parser doesn't depend on framing data, it'll work smoother with any source (testbench bytes, raw replays, deframer, no deframer, etc.).
- Reason 2: Cross checking always helps!
- Since msg_length() as written is a combinational lookup completed in parallel and in the same cycle the type byte arrives, so there's no latency overhead here. A comparator would cost some area and I'd have to figure out what to do on mismatch, but either way the latency (in cycles) is fine.

### L-10: Wire Format (08.03.26)
- The parser converts on-wire encodings to internal representations, and the raw protocol format disappears downstream. More specifically, the ASCII message type byte is a 4-bit enum, the Buy/Sell is a single bit. So all protocol knowledge is designed to live in ONE module, maybe this will be more valuable on a speculation of revised type codes or feeding the same orderbook a different exchange protocol.
- Although, price is NOT decoded (still raw) and the tracking number is dropped entirely (NASDAQ's spec says its Nasdaq-internal so its a dead wire to worry about).

### L-11 Deconstructing Messages (08.03.26)
- I'm keeping explicit (~60) case arms, one per byte to route that byte into a specific part of a specific field. It's verbose and I could collapse it in the future (shift incoming bytes into a buffer and extract fields once message completes, use macros to generate repetitive arms, generate RTL from machine-readable spec descriptions, etc.), BUT I want every line to map to exactly ONE row of a spec table.
- Any later optimization should be checked against this explicit version which is verifiable by inspection. We'll go for correctness first, and compress later. 

### Sync Active LO Reset (08.03.26)
- Registers reset syncrhonously on clock edge, using the active-low version, `rst_n`. It's recommended on UltraScale devices for arhitectural purposes: the flip-flop primitives have a dedicated sync reset input, so using a sync reset costs no extra logic while a typically async reset would have to route differently.
- And a second choice is part of this, we only reset signals that must be deterministic. Namely, FSM state, byte counter, output valid. I've opted NOT to reset the ~325 payload flip-flops, leaving it more to a "who cares until they're written" philosophy, since resetting them costs area and timing for absolutely no purpose.
- In the case of ASIC though, the ASIC libraries would prefer async assert with sync deassert since reset must take effect before the clock is running. I currently have sights to test my parser in my research lab via Cadence flows (to get a PPA measurement (Power, Performance, Area)), where Genus might honestly object to this design choice. 