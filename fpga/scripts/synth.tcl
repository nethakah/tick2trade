# set - variables
set part xczu7ev-ffvc1156-2-e
set top tick2trade_top
set outdir ./fpga/results

file mkdir $outdir

# read_verilog - load packages, rtl, constraints
read_verilog -sv rtl/msg_pkg.sv
read_verilog -sv rtl/skid_buffer.sv
read_verilog -sv rtl/async_fifo.sv
read_verilog -sv rtl/moldudp_deframer.sv
read_verilog -sv rtl/itch_parser.sv
read_verilog -sv rtl/order_book.sv
read_verilog -sv rtl/trade_signal.sv
read_verilog -sv rtl/tick2trade_top.sv

read_xdc fpga/constraints/tick2trade.xdc

# turn RTL into FPGA primitives
# out_of_context so we synthesize this module alone with no pin placement or block design
synth_design -top $top \
             -part $part \
             -mode out_of_context

# how much of the chip this uses
# make sure the L3 table shows up as distributed RAM instead of BRAM
# -hierarchical to break it down per module
report_utilization -file $outdir/utilization.rpt
report_utilization -hierarchical -file $outdir/utilization_hier.rpt

# whether clk constraint was met
# WNS = margin on tightest path (pos = there's headroom, neg = cannot run this fast)
report_timing_summary -file $outdir/timing_synth.rpt

# says whether order book's arrays inferred as BRAM or URAM or fell to LUTs
report_ram_utilization -file $outdir/ram.rpt

# saved snapshot on synthesized design
write_checkpoint -force $outdir/post_synth.dcp

puts "SYNTHESIS COMPLETE; reports saved to $outdir"