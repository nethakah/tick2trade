#==============================================================================
# package_ip.tcl - wrap the RTL as an IP block so the block design can use it.
#
# Run from the REPO ROOT:  vivado -mode batch -source fpga/scripts/package_ip.tcl
#
# Vivado's block designer works with IP, not loose RTL files. This produces
# fpga/ip/, a directory of metadata saying "this module has an AXI4-Stream
# slave here, an AXI4-Lite slave there, these clocks, these resets".
#
# The interfaces are INFERRED from port name prefixes: s_axis_* becomes AXI4-
# Stream and s_axi_* becomes AXI4-Lite. That is why the naming convention
# mattered - 34 loose ports collapse into two connectable interfaces.
#==============================================================================

# Scratch project. The IP packager needs a project context; only fpga/ip/ is kept.
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

# Tell Vivado which clock drives which interface. Without this the block design
# cannot check frequencies or auto-wire clocks, and validation complains that
# the AXI interfaces have no FREQ_HZ parameter.
#   s_axis is DMA ingress -> dma_clk (the FIFO write side)
#   s_axi  is the CSR     -> core_clk
ipx::associate_bus_interfaces -busif s_axis -clock dma_clk [ipx::current_core]
ipx::associate_bus_interfaces -busif s_axi -clock core_clk [ipx::current_core]

ipx::save_core [ipx::current_core]
puts "(!) IP packaged to fpga/ip"
