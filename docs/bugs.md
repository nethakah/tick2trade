# Bug Journal

---

### bug-03: Output fields above `shares` read as `expected << 1`
- Symptom: On the first parser test dump of all 12 words of `m_axis_tdata`, words 2-5 were fine, but words 6-11 were ALL wrong. 
- Cause: Fields land on a 32-bit boundary, and I noticed every field was exactly `<< 1`, not anything more scrambled than that.
- Fix: `is_buy` must be declared at the right point in the packed struct, beside msg_type in the top word, where `rsvd0` has space to absorb it. 

### bug-02: Verilator reference to msg_t
- Symptom: Verilator --lint-only -Wall rtl/*.sv errored at itch_parser.sv
- Cause: Shell expands rtl/*.sv alphabetically, and we need to compile the packages (msg_pkg.sv) before the files using them in SV since SV uses import as a TEXT COPY AND PASTE!
- Fix: Compile packages first.

### bug-01: Off-by-one on ITCH field offset (18 vs. 19)
- Symptom: Computed E's executed shares starting at offset 18.
- Cause: Reasoned from where prev field ENDED rather than running sums; the order reference number starts at 11 and is 8 bytes, so [11-18] are occupied and the next field starts at 11+8=19.
- Fix: 18 -> 19