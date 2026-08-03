#!/usr/bin/env bash
set -e
PKGS="rtl/msg_pkg.sv"   # packages ALWAYS first — dependents reference their types

verilator --lint-only -Wall --top-module itch_parser $PKGS rtl/itch_parser.sv
verilator --lint-only -Wall --top-module async_fifo  rtl/async_fifo.sv

echo "lint clean"