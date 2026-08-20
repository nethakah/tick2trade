# tightening to 3ns = 333MHz (post synth gave 7.377ns slack so we closed at 2.623ns)
create_clock -period 3.000 -name core_clk [get_ports core_clk]
# 10ns (100MHz) placeholder for synthesis (DMA clk comes from PS's AXI-HP interface)
create_clock -period 10.000 -name dma_clk [get_ports dma_clk]

# async cdc
set_clock_groups -asynchronous \
    -group [get_clocks core_clk] \
    -group [get_clocks dma_clk]

# drive the clocks by the block buffer at the sites listed below
# note if u set with invalid site name itll fail silently
set_property HD.CLK_SRC BUFGCE_HDIO_X0Y2 [get_ports core_clk]
set_property HD.CLK_SRC BUFGCE_HDIO_X0Y3 [get_ports dma_clk]
