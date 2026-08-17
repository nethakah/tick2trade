#!/usr/bin/env bash
set -e
PKGS="rtl/msg_pkg.sv"   # packages ALWAYS first — dependents reference their types

verilator --lint-only -Wall --top-module itch_parser rtl/msg_pkg.sv rtl/skid_buffer.sv rtl/itch_parser.sv
verilator --lint-only -Wall --top-module async_fifo  rtl/async_fifo.sv
verilator --lint-only -Wall --top-module order_book rtl/msg_pkg.sv rtl/order_book.sv

echo "lint clean"