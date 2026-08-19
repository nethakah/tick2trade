module tick2trade_top
    import msg_pkg::*;
(
    // ingress clock domain - at PS's AXI-HP interface clock
    input logic dma_clk,
    input logic dma_rst_n,

    // axi4-stream slave - raw MoldUDP64 packet bytes from DMA
    input logic[7:0] s_axis_tdata,
    input logic s_axis_tvalid,
    input logic s_axis_tlast,
    output logic s_axis_tready,

    // core clock domain - PL
    input logic core_clk,
    input logic core_rst_n,

    // configurables (software writes this over axi4-lite)
    input logic cfg_armed,
    input logic cfg_side,
    input logic[31:0] cfg_trigger_price,
    input logic[31:0] cfg_order_shares,
    input logic[31:0] cfg_spread_max,
    input logic[31:0] cfg_size_min,

    // fired/loaded order
    output logic order_fire,
    output logic order_side,
    output logic[31:0] order_price,
    output logic[31:0] order_shares,

    // status (software reads this over axi4-lite)
    output logic[31:0] best_bid_price,
    output logic[31:0] best_ask_price,
    output logic[31:0] best_bid_shares,
    output logic[31:0] best_ask_shares,
    output logic[31:0] spread,
    output logic[31:0] fire_count,
    output logic[63:0] sequence_num,
    output logic gap_detected,
    output logic[31:0] gap_count,
    output logic[31:0] packet_count,
    output logic packet_error,
    output logic[31:0] overflow_count,
    output logic[31:0] miss_count,
    output logic[31:0] level_collision_count
);

    // fifo to deframer (core_clk domain)
    logic[7:0] fifo_data;
    logic fifo_read;
    logic fifo_empty;
    logic fifo_full;

    // deframer to parser
    logic[7:0] deframer_in_tdata;
    logic deframer_in_tvalid;
    logic deframer_in_tready;

    // parser to orderbook
    msg_t parser_tdata;
    logic parser_tvalid;
    logic parser_tready;

    // orderbook to signal
    logic[31:0] book_bid_price;
    logic[31:0] book_ask_price;
    logic[31:0] book_bid_shares;
    logic[31:0] book_ask_shares;
    logic book_valid;

    assign deframer_in_tvalid = !fifo_empty;
    assign deframer_in_tdata = fifo_data;
    assign fifo_read = deframer_in_tvalid && deframer_in_tready;



endmodule