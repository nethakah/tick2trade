# Bug Journal
Linting: `verilator --lint-only -Wall --top-module modulename rtl/msg_pkg.sv rtl/modulename.sv`

---

### bug-01: Off-by-one on ITCH field offset (18 vs. 19)
- Symptom: Computed E's executed shares starting at offset 18.
- Cause: Reasoned from where prev field ENDED rather than running sums; the order reference number starts at 11 and is 8 bytes, so `[11-18]` are occupied and the next field starts at 11+8=19.
- Fix: 18 -> 19

### bug-02: Verilator reference to msg_t
- Symptom: Verilator --lint-only -Wall rtl/*.sv errored at itch_parser.sv
- Cause: Shell expands rtl/*.sv alphabetically, and we need to compile the packages (msg_pkg.sv) before the files using them in SV since SV uses import as a TEXT COPY AND PASTE!
- Fix: Compile packages first.

### bug-03: Output fields above `shares` read as `expected << 1`
- Symptom: On the first parser test dump of all 12 words of `m_axis_tdata`, words 2-5 were fine, but words 6-11 were ALL wrong. 
- Cause: Fields land on a 32-bit boundary, and I noticed every field was exactly `<< 1`, not anything more scrambled than that.
- Fix: `is_buy` must be declared at the right point in the packed struct, beside msg_type in the top word, where `rsvd0` has space to absorb it. 

### bug-04: Messages destroyed in downstream stall
- Symptom: Backpressure test was losing 1/3 messages; `s_axis_tready` didn't drop and `m_axis_tvalid` sat high from byte 37-77 while the FSM kept consuming.
- Cause: `s_axis_tready = !(fsm_tvalid && !fsm_tready)` stalled when skid buffer was full, but the skid holds 2 slots so we were still reporting ready with 1 message, so the FSM completed a 2nd message and overwrote `fsm_tdata` without transferring the first message.
- Fix: `s_axis_tready = !fsm_tvalid` so we stall on our own pending output rather than the remaining capacity. Unfortunately this costs ~1 cycle per msg but does buy the guarantee we need for AXI.
- Notes for me: producer SHOULD NOT overwrite data it already asserted valid!!!

### bug-05: TB blind to cycles inside stalled push
- Symptom: After bug-04, tests that used to work started all failing. Like randomized tests ALL failed.
- Cause: Before bug-04 fix, `push_byte` was 1 cycle precisely, so we'd check after push_byte returned on every cycle, but now that the parser was properly stalling, `push_byte` looped internally so messages emitted during stall cycles became "invisisble" in a sense to the outside (since `m_axis_tvalid` is HI for 1 cycle).
- Fix: Check `m_axis_tvalid && m_axis_tready` inside retry loop (in `feed_byte` and inline in tests) and a per-byte guard.
- Notes for me: if you write a helper that consumes a variable amount of cycles, it HAS TO CHECK ALL OF THEM because the caller cannot.

### bug-06: Parser consuming bytes while not ready for them
- Symptom: 100k random msgs failed with 224 msgs missing and weird ASCII spaces in the timestamps and ticker fragments in `locate`. Fixed tests did mostly pass though.
- Cause: FSM was gated on `if (s_axis_tvalid)`. But from bug-04, we got that `s_axis_tready = !fsm_tvalid` so the parser was telling upstream that its not ready while its own FSM was consuming bytes regardless. The TB failed because it was holding the bytes correctly and re-sending so the FSM was consuming the same type bytes TWICE and the rest of the message was shifting as a result.
- Fix: `if (s_axis_tvalid && s_axis_tready)` so a transfer happens when both are HI.
- Notes for me: in this case, seeing ASCII in the numeric field was a clear indicator that the parser was somehow misaligned rather than a bigger error like extracting wrong, so look at boundaries in cases like this.

### bug-07: Ask-side rescan
- Symptom: Completely lifecycle test (#11) outputted the ask at 1230500 with 400 shares after that level was executed, even though it should've moved to 1231000 with 500 shares. Bid-side tests passed fine.
- Cause: Rescan checked curr_price == top_bid_price only.
- Fix: Need symmetric logic for symmetric tests. 

### bug-08: Testbench state leaking
- Symptom: 16 fails when running TB for the order_book. Test #4 pushed one 500-share order and got 1500, which I noticed was test2's 500 + test3's 500 + test4's 500, which all aggregated into one level but between tests.
- Cause: memory is not reset in the RTL; reset() asserts on the rst_n port, which clears the FSM and counters and registers for the top of the book, but it DOESN'T CLEAR book_mem or bid_levels or ask_levels (these are not reset in the RTL as per log-12, resetting memory is a waste of area and messes with BRAM inferencing).
- Fix: use a fresh Vorder_book on every test.
- Notes for me: not an RTL bug, and I didn't see anything like this in the other testbenches because their states did not survive reset AND get read. Here they do, so that's a problem to be reusing the same "dut" then calling all the test functions on it.

### bug-09: SVA assumed book_valid led price change
- Symptom: `!$stable(best_bid_price) |-> $past(book_valid)` fired immediately in order book. 
- Cause: book_valid and top_bid_price assigned on same edge in UPDATE state so they both are visible together, so we don't need to do $past (looks at the cycle before that). 
- Fix: drop $past.
- Notes for me: Couple of bugs like this, make sure asserts in SV are correct in terms of timing!!!

### bug-10: Testbench sampled fire pulse 1 cycle late
- Symptom: In trade_signal tb 7 failures on testing which were that it didn't fire when it should, but TEST9 showed fire_count=1 so RTL was definitely firing.
- Cause: market_update() ticked twice before evaluating order_fire, so first tick FSM saw book_valid and scheduled order_fire <= 1 but second tick makes it visible AND schedules it back to 0 so by the time we read, the pulse was gone.
- Fix: eval() instead of tick() on that line, so we recompute outputs without advancing time.
- Notes for me: you need to sample 1-cycle pulses on the cycle (like bug-05) it's high and you have to read it before the advance on the clock.

### bug-11: set_property fails silently on bad site name
- Symptom: added HD.CLK_SRC to fix empty timing summary.
- Cause: Didn't check for real sites on this device; vivado accepted a nonexistent one and ran normally.
- Fix: real sites findable via `get_sites -filter {SITE_TYPE =~ *BUFG*}`.