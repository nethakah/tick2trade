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
    localparam int INGRESS_WIDTH = 9; // 8 tdata + 1 tlast
    localparam int INGRESS_DEPTH = 16; // enough depth for CDC not buffering (log-17)

    // fifo to deframer (core_clk domain)
    logic[INGRESS_WIDTH-1:0] fifo_data;
    logic fifo_read;
    logic fifo_empty;
    logic fifo_full;

    logic[7:0] deframer_in_tdata;
    logic deframer_in_tlast;
    logic deframer_in_tvalid;
    logic deframer_in_tready;

    // deframer to parser
    logic[7:0] deframer_tdata;
    logic deframer_tvalid;
    logic deframer_tready;

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

    // fifo to axi-stream adaptor
    assign deframer_in_tvalid = !fifo_empty;
    assign deframer_in_tdata = fifo_data;
    assign fifo_read = deframer_in_tvalid && deframer_in_tready;

    /* 
    Async FIFO
    */
    async_fifo #(
        .DATA_WIDTH(INGRESS_WIDTH),
        .DEPTH(INGRESS_DEPTH)
    ) u_fifo(
        // in (write domain driven by DMA)
        .w_clk(dma_clk),
        .w_rst_n(dma_rst_n),
        .w_enbl(s_axis_tvalid && s_axis_tready),
        .w_data({s_axis_tlast, s_axis_tdata}), // pack tlast in MSB
        
        // out (to the DMA so it knows to pause)
        .full(fifo_full),

        // in (read domain driven by _top)
        .r_clk(core_clk),
        .r_rst_n(core_rst_n),
        .r_enbl(fifo_read),

        // out (to the deframer adapter below)
        .r_data(fifo_data),
        .empty(fifo_empty)
    );

    // backpressure to DMA
    assign s_axis_tready = !fifo_full;

    // FIFO to axi4-stream adapter
    assign deframer_in_tvalid = !fifo_empty;
    assign deframer_in_tdata = fifo_data[7:0];
    assign deframer_in_tlast = fifo_data[8];
    assign fifo_read = deframer_in_tvalid && deframer_in_tready;

    /*
    MoldUDP64 deframer
    */
    moldudp_deframer u_deframer(
        .clk(core_clk),
        .rst_n(core_rst_n),

        // in (raw packet bytes from FIFO)
        .s_axis_tdata(deframer_in_tdata),
        .s_axis_tvalid(deframer_in_tvalid),
        .s_axis_tlast(deframer_in_tlast),
        // out (backpressure to FIFO adapter)
        .s_axis_tready(deframer_in_tready),

        // out (ITCH bytes to parser)
        .m_axis_tdata(deframer_tdata),
        .m_axis_tvalid(deframer_tvalid),
        // in (backpressure from parser)
        .m_axis_tready(deframer_tready),

        // out (status ports for the software to read)
        .sequence_num(sequence_num),
        .gap_detected(gap_detected),
        .gap_count(gap_count),
        .packet_error(packet_error),
        .packet_count(packet_count)
    );

    /*
    ITCH parser
    */
    itch_parser u_parser(
        .clk(core_clk),
        .rst_n(core_rst_n),

        // in (itch bytes from deframer)
        .s_axis_tdata(deframer_tdata),
        .s_axis_tvalid(deframer_tvalid),
        // out (backpressure to deframer)
        .s_axis_tready(deframer_tready),

        // out (decoded msg to book)
        .m_axis_tdata(parser_tdata),
        .m_axis_tvalid(parser_tvalid),
        // in (backpressure fromm book)
        .m_axis_tready(parser_tready)
    );

    /*
    Order book
    */

    order_book u_book(
        .clk(core_clk),
        .rst_n(core_rst_n),

        // in (decoded msgs from parser)
        .s_axis_tdata(parser_tdata),
        .s_axis_tvalid(parser_tvalid),
        // out (backpressure to parser)
        .s_axis_tready(parser_tready),

        // out (topofbook goes to signal stage and to software)
        .best_bid_price(book_bid_price),
        .best_ask_price(book_ask_price),
        .best_bid_shares(book_bid_shares),
        .best_ask_shares(book_ask_shares),
        .book_valid(book_valid),

        // out (status/checks signals)
        .overflow_count(overflow_count),
        .miss_count(miss_count),
        .level_collision_count(level_collision_count)
    );

    // _top output ports assigned
    assign best_bid_price = book_bid_price;
    assign best_ask_price = book_ask_price;
    assign best_bid_shares = book_bid_shares;
    assign best_ask_shares = book_ask_shares;

    /*
    Trade Signal
    */
    trade_signal u_signal(
        .clk(core_clk),
        .rst_n(core_rst_n),

        // in (market state from orderbook)
        .best_bid_price(book_bid_price),
        .best_ask_price(book_ask_price),
        .best_bid_shares(book_bid_shares),
        .best_ask_shares(book_ask_shares),
        .book_valid(book_valid),

        // in (cfg order and gates from software)
        .cfg_armed(cfg_armed),
        .cfg_side(cfg_side),
        .cfg_trigger_price(cfg_trigger_price),
        .cfg_order_shares(cfg_order_shares),
        .cfg_spread_max(cfg_spread_max),
        .cfg_size_min(cfg_size_min),

        // out (fired order)
        .order_fire(order_fire),
        .order_side(order_side),
        .order_price(order_price),
        .order_shares(order_shares),

        // out (status signals)
        .spread(spread),
        .fire_count(fire_count)
    );
endmodule