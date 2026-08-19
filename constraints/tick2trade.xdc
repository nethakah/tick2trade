# Starting target loose: 100MHz
create_clock -period 10.000 -name core_clk [get_ports core_clk]
# Placeholder: DMA clock is whatever Zynq block design's AXi-HP interface is configured to
create_clock -period 10.000 -name dma_clk [get_ports dma_clk]

# async cdc
set_clock_groups -asynchronous \
    -group [get_clocks core_clk] \
    -group [get_clocks dma_clk]

