# top level constraints for block design
# parsed after every ip's xdc
# clk_pl_0 feeds DMA and fifo write; clk_pl_1 feeds core and fifo read

set_clock_groups -asynchronous \
    -group [get_clocks clk_pl_0] \
    -group [get_clocks clk_pl_1]