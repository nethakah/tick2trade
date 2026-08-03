module async_fifo #(
    parameter int DATA_WIDTH = 8,
    parameter int DEPTH = 16

)(
    input logic w_clk,
    input logic w_rst_n,
    input logic w_enbl,
    input logic [DATA_WIDTH-1:0] w_data,
    output logic full,

    input logic r_clk,
    input logic r_rst_n,
    input logic r_enbl,
    output logic [DATA_WIDTH-1:0] r_data,
    output logic empty
);

endmodule
