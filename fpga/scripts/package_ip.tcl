# vivado -mode batch -source fpga/scripts/package_ip.tcl

# scratch
create_project -force ip_pkg /scratch/$::env(USER)/ip_pkg -part xczu7ev-ffvc1156-2-e

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
set_property top tick2trade_top [current_fileset]
update_compile_order -fileset sources_1

ipx::package_project -root_dir [pwd]/fpga/ip -vendor cmu -library user -taxonomy /UserIP -force

ipx::infer_bus_interfaces xilinx.com:interface:axis_rtl:1.0 [ipx::current_core]
ipx::infer_bus_interfaces xilinx.com:interface:aximm_rtl:1.0 [ipx::current_core]

# tell vivado which clock drives which interface 
ipx::associate_bus_interfaces -busif s_axis -clock dma_clk [ipx::current_core]
ipx::associate_bus_interfaces -busif s_axi -clock core_clk [ipx::current_core]

ipx::save_core [ipx::current_core]
puts "(!) IP packaged successfully"
