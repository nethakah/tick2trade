#!/usr/bin/env bash
set -e
PKGS="rtl/msg_pkg.sv" # packages first bc we reference those types
FLAGS="--lint-only -Wall --assert +define+SIM"

verilator $FLAGS --top-module tick2trade_top $PKGS rtl/skid_buffer.sv rtl/async_fifo.sv rtl/moldudp_deframer.sv rtl/itch_parser.sv rtl/order_book.sv rtl/trade_signal.sv rtl/tick2trade_csr.sv rtl/tick2trade_top.sv
verilator $FLAGS --top-module itch_parser $PKGS rtl/skid_buffer.sv rtl/itch_parser.sv
verilator $FLAGS --top-module moldudp_deframer $PKGS rtl/moldudp_deframer.sv
verilator $FLAGS --top-module order_book $PKGS rtl/order_book.sv
verilator $FLAGS --top-module trade_signal $PKGS rtl/trade_signal.sv
verilator $FLAGS --top-module tick2trade_csr rtl/tick2trade_csr.sv
verilator $FLAGS --top-module async_fifo rtl/async_fifo.sv
verilator $FLAGS --top-module skid_buffer rtl/skid_buffer.sv

echo "(!) Lint clean"
