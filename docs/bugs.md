# Bug Journal

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
