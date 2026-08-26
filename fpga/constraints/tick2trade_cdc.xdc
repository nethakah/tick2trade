# package w IP for block design use (declares crossing async)

set_clock_groups -asynchronous \
    -group [get_clocks -of_objects [get_ports core_clk]] \
    -group [get_clocks -of_objects [get_ports dma_clk]]