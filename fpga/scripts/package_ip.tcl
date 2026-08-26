# vivado -mode batch -source fpga/scripts/package_ip.tcl

# scratch space for ip packager to work
create_project -force ip_pkg /scratch/$::env(USER)/ip_pkg -part xczu7ev-ffvc1156-2-e

# -norecurse bc explicit paths not directory
add_files -norecurse {
    rtl/msg_pkg.sv
    rtl/skid_buffer.sv
    rtl/async_fifo.sv
    rtl/moldudp_deframer.sv
    rtl/itch_parser.sv
    rtl/order_book.sv
    rtl/trade_signal.sv
    rtl/tick2trade_csr.sv
    rtl/tick2trade_top.sv
}

# set top file property for vivado to know which of 9 it is
set_property top tick2trade_top [current_fileset]
update_compile_order -fileset sources_1

# writes ip metadata to fpga/ip; part after is string for block design to stantiate (VLNV)
ipx::package_project -root_dir [pwd]/fpga/ip -vendor cmu -library user -taxonomy /UserIP -force

# read port name prefixes and group into interfaces appropriately
ipx::infer_bus_interfaces xilinx.com:interface:axis_rtl:1.0 [ipx::current_core]
ipx::infer_bus_interfaces xilinx.com:interface:aximm_rtl:1.0 [ipx::current_core]

# tell vivado which clock drives which interface 
ipx::associate_bus_interfaces -busif s_axis -clock dma_clk [ipx::current_core]
ipx::associate_bus_interfaces -busif s_axi -clock core_clk [ipx::current_core]

# write the metadata out
ipx::save_core [ipx::current_core]
puts "(!) IP packaged successfully"
