module itch_parser 
    import msg_pkg::*;
(
    input logic clk,
    input logic rst_n, // active LO

    // Slave - raw ITCH byte stream in
    input logic [7:0] s_axis_tdata,
    input logic       s_axis_tvalid,
    output logic      s_axis_tready, // backpressure

    // Master - parsed msg out
    output msg_t      m_axis_tdata, // 325b packed msg
    output logic      m_axis_tvalid,
    input logic       m_axis_tready
);

endmodule
