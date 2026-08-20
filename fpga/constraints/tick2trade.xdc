# placeholders / loose starting point
create_clock -period 10.000 -name core_clk [get_ports core_clk]
create_clock -period 10.000 -name dma_clk [get_ports dma_clk]

# async cdc
set_clock_groups -asynchronous \
    -group [get_clocks core_clk] \
    -group [get_clocks dma_clk]

# drive the clocks by the block buffer at the sites listed below "BUFG..."
set_property HD.CLK_SRC BUFGCE_X0Y0 [get_ports core_clk]
set_property HD.CLK_SRC BUFGCE_X0Y1 [get_ports dma_clk]
