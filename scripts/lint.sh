#!/usr/bin/env bash
set -e
PKGS="rtl/msg_pkg.sv"   # packages ALWAYS first — dependents reference their types

verilator --lint-only -Wall --top-module itch_parser rtl/msg_pkg.sv rtl/skid_buffer.sv rtl/itch_parser.sv
verilator --lint-only -Wall --top-module async_fifo rtl/msg_pkg.sv rtl/async_fifo.sv
verilator --lint-only -Wall --top-module order_book rtl/msg_pkg.sv rtl/order_book.sv
verilator --lint-only -Wall --top-module moldudp_deframer rtl/msg_pkg.sv rtl/moldudp_deframer.sv
verilator --lint-only -Wall --top-module trade_signal rtl/msg_pkg.sv rtl/trade_signal.sv

echo "lint clean"