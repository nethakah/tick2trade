module itch_parser 
(
    input logic clk,
    input logic rst_n, // active LO

    input logic [7:0] byte_in,
    input logic byte_valid,

    output logic        msg_valid,
    output msg_type_e   msg_type,
    output logic [15:0] msg_locate, // order book array index (stock locate)
    output logic [47:0] msg_timestamp,
    output logic [63:0] msg_order_refnum,
    output logic        msg_is_buy,
    output logic [31:0] msg_shares,
    output logic [63:0] msg_stock,
    output logic [31:0] msg_price,
    output logic [63:0] msg_match_num
);

endmodule
