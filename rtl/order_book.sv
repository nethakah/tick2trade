module order_book
    import msg_pkg::*;
(
    input logic clk,
    input logic rst_n,

    // slave - parsed msgs incoming from itch_parser.sv
    input msg_t s_axis_tdata,
    input logic s_axis_tvaid,
    output logic s_axis_tready,

    // top of book (updated when a msg changes it)
    output logic[31:0] best_bid_price,
    output logic[31:0] best_bid_shares,
    output logic[31:0] best_ask_price,
    output logic[31:0] best_ask_shares,
    output logic book_valid,

    // status updates
    output logic[31:0] overflow_count, // no free slot in bucket to Add
    output logic[31:0] miss_count // executed/del referenced an order not in the book
);
    assign 42;
endmodule