// Control/Status registers - drive trade_signal's cfg_* ports via AXI4-Lite (memory addressed)

module tick2trade_csr #(
    parameter int ADDR_WIDTH = 8 
    // AXI-lite is byte addressed so 8 bits covers 64 regs
)(
    input logic clk,
    input logic rst_n,

    // AW - Write Address
    input logic[ADDR_WIDTH-1:0] s_axi_awaddr,
    input logic s_axi_awvalid,
    output logic s_axi_awready,

    // W - Write data channel
    input logic[31:0] a_axi_wdata,
    input logic[3:0] s_axi_wstrb,
    input logic s_axi_wvalid,
    output logic s_axi_wready,

    // B - Write response channel
    output logic[1:0] s_axi_bresp,
    output logic s_axi_bvalid,
    input logic s_axi_bready,

    // AR - Read address channel
    input logic[ADDR_WIDTH-1:0] s_axi_araddr,
    input logic s_axi_arvalid,
    output logic s_axi_arready,

    // R - Read data channel
    output logic[31:0] s_axi_rdata,
    output logic[1:0] s_axi_rresp,
    output logic s_axi_rvalid,
    input logic s_axi_rready,

    // Cfg output to trade_signal and order_book
    output logic cfg_armed,
    output logic cfg_side,
    output logic[31:0] cfg_trigger_price,
    output logic[31:0] cfg_order_shares,
    output logic[31:0] cfg_spread_max,
    output logic[31:0] cfg_size_min,
    output logic[15:0] cfg_stock_locate,

    // Status in from pipeline
    input logic[31:0] best_bid_price,
    input logic[31:0] best_ask_price,
    input logic[31:0] best_bid_shares,
    input logic[31:0] best_ask_shares,
    input logic[31:0] spread,
    input logic[31:0] fire_count,
    input logic[31:0] packet_count,
    input logic[31:0] gap_count,
    input logic[31:0] miss_count,
    input logic[31:0] overflow_count,
    input logic[31:0] level_collision_count
);



endmodule