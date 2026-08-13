module itch_parser 
    import msg_pkg::*;
(
    input logic clk,
    input logic rst_n, // active LO

    // Slave - raw ITCH byte stream in
    // transfer only on tvalid && tready
    input logic [7:0] s_axis_tdata, // 1 ITCH byte
    input logic       s_axis_tvalid, // upstream (MoldUDP64 deframer or tb): validate byte is real
    output logic      s_axis_tready, // us: always 1 for now (we can always accept 1 byte)

    // Master - parsed msg out
    output msg_t      m_axis_tdata, // 325b packed msg
    output logic      m_axis_tvalid, // us: bundle is valid
    input logic       m_axis_tready // downstream (orderbook): it can accept stuff
);
    typedef enum logic [0:0] {
        READ_TYPE = 1'b0, // incoming byte is msg type code (offset 0)
        READ_BODY = 1'b1 // incoming byte is a field byte of curr msg
    } state_enum;

    state_enum state;
    logic [5:0] byte_index; // offset of current byte arriving
    logic [5:0] curr_len; // len of msg being consumed

    msg_t fsm_tdata; // FSM's output (feeds skid buffer (slave))
    logic fsm_tvalid;
    logic fsm_tready; // skid buffer telling FSM whether it can accept

    // dont accept bytes when we're holding output which the skid buffer cannot take
    // skid fills --> parser stops consuming --> FIFO fills --> DMA pauses = lossless
    assign s_axis_tready = !fsm_tvalid; // since fsm_tvalid is only high when a msg is waiting to handoff

    // padding added for testbench C++ extraction from bits easier
    assign fsm_tdata.rsvd0 = '0;
    assign fsm_tdata.rsvd1 = '0;
    assign fsm_tdata.rsvd2 = '0;

    // THE FSM (read bytes and fill-in fields) //
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state <= READ_TYPE;
            byte_index <= 6'd0;
            if (fsm_tready) begin // only hold high when waiting for acceptance (not dropping after 1 cycle)
                fsm_tvalid <= 1'b0;
            end
        end else begin
            fsm_tvalid <= 1'b0;

            // only transfer when tvalid && tready
            if (s_axis_tvalid && s_axis_tready) begin
                case (state)
                    READ_TYPE: begin
                        if (msg_length(s_axis_tdata) != 6'd0) begin // recognized type
                            fsm_tdata.msg_type <= decode_type(s_axis_tdata);
                            curr_len <= msg_length(s_axis_tdata);
                            byte_index <= 6'd1;
                            state <= READ_BODY;
                        end
                        // else not recognized; stay in READ_TYPE (CHANGE IN V2!)
                    end

                    READ_BODY: begin
                        // identical for A/E/D
                        case (byte_index)
                            // Stock locate @1 (2B)
                            6'd1: fsm_tdata.stock_locate[15:8] <= s_axis_tdata;
                            6'd2: fsm_tdata.stock_locate[7:0] <= s_axis_tdata;

                            // Tracking num @3 (2B)
                            6'd3: ;
                            6'd4: ;
                            
                            // Timestamp @5 (6B)
                            6'd5: fsm_tdata.timestamp[47:40] <= s_axis_tdata;
                            6'd6: fsm_tdata.timestamp[39:32] <= s_axis_tdata;
                            6'd7: fsm_tdata.timestamp[31:24] <= s_axis_tdata;
                            6'd8: fsm_tdata.timestamp[23:16] <= s_axis_tdata;
                            6'd9: fsm_tdata.timestamp[15:8] <= s_axis_tdata;
                            6'd10: fsm_tdata.timestamp[7:0] <= s_axis_tdata;

                            // Order ref num @11 (8B)
                            6'd11: fsm_tdata.order_ref_num[63:56] <= s_axis_tdata;
                            6'd12: fsm_tdata.order_ref_num[55:48] <= s_axis_tdata;
                            6'd13: fsm_tdata.order_ref_num[47:40] <= s_axis_tdata;
                            6'd14: fsm_tdata.order_ref_num[39:32] <= s_axis_tdata;
                            6'd15: fsm_tdata.order_ref_num[31:24] <= s_axis_tdata;
                            6'd16: fsm_tdata.order_ref_num[23:16] <= s_axis_tdata;
                            6'd17: fsm_tdata.order_ref_num[15:8] <= s_axis_tdata;
                            6'd18: fsm_tdata.order_ref_num[7:0] <= s_axis_tdata;

                            default: ;
                        endcase

                        case (fsm_tdata.msg_type)
                            MSG_ADD: begin
                                case (byte_index)
                                    6'd19: fsm_tdata.is_buy <= (s_axis_tdata == "B");

                                    // Shares @20 (4B)
                                    6'd20: fsm_tdata.shares[31:24] <= s_axis_tdata;
                                    6'd21: fsm_tdata.shares[23:16] <= s_axis_tdata;
                                    6'd22: fsm_tdata.shares[15:8] <= s_axis_tdata;
                                    6'd23: fsm_tdata.shares[7:0] <= s_axis_tdata;

                                    // Stock @24 (8B)
                                    6'd24: fsm_tdata.stock[63:56] <= s_axis_tdata;
                                    6'd25: fsm_tdata.stock[55:48] <= s_axis_tdata;
                                    6'd26: fsm_tdata.stock[47:40] <= s_axis_tdata;
                                    6'd27: fsm_tdata.stock[39:32] <= s_axis_tdata;
                                    6'd28: fsm_tdata.stock[31:24] <= s_axis_tdata;
                                    6'd29: fsm_tdata.stock[23:16] <= s_axis_tdata;
                                    6'd30: fsm_tdata.stock[15:8] <= s_axis_tdata;
                                    6'd31: fsm_tdata.stock[7:0] <= s_axis_tdata;

                                    // Price @32 (4B) (KEY NOTE: 4 IMPLIED DECIMALS (fixed pt))
                                    6'd32: fsm_tdata.price[31:24] <= s_axis_tdata;
                                    6'd33: fsm_tdata.price[23:16] <= s_axis_tdata;
                                    6'd34: fsm_tdata.price[15:8] <= s_axis_tdata;
                                    6'd35: fsm_tdata.price[7:0] <= s_axis_tdata;

                                    default: ;
                                endcase
                            end

                            MSG_EXC: begin
                                case (byte_index)
                                    6'd19: fsm_tdata.shares[31:24] <= s_axis_tdata;
                                    6'd20: fsm_tdata.shares[23:16] <= s_axis_tdata;
                                    6'd21: fsm_tdata.shares[15:8] <= s_axis_tdata;
                                    6'd22: fsm_tdata.shares[7:0] <= s_axis_tdata;

                                    // Match num @23 (8B) (the unique day execution ID)
                                    6'd23: fsm_tdata.match_num[63:56] <= s_axis_tdata;
                                    6'd24: fsm_tdata.match_num[55:48] <= s_axis_tdata;
                                    6'd25: fsm_tdata.match_num[47:40] <= s_axis_tdata;
                                    6'd26: fsm_tdata.match_num[39:32] <= s_axis_tdata;
                                    6'd27: fsm_tdata.match_num[31:24] <= s_axis_tdata;
                                    6'd28: fsm_tdata.match_num[23:16] <= s_axis_tdata;
                                    6'd29: fsm_tdata.match_num[15:8] <= s_axis_tdata;
                                    6'd30: fsm_tdata.match_num[7:0] <= s_axis_tdata;

                                    default: ;
                                endcase
                            end

                            MSG_DEL: ;
                            MSG_NONE: ;
                            default: ;
                        endcase // END OF MESSAGE! 
                        
                        // curr_len = total bytes
                        if (byte_index == curr_len - 6'd1) begin
                            fsm_tvalid <= 1'b1;
                            state <= READ_TYPE;
                        end else begin
                            byte_index <= byte_index + 6'd1;
                        end
                    end
                endcase
            end
        end
    end

    skid_buffer #(
        .DATA_WIDTH ($bits(msg_t))
    ) sb (
        .clk (clk),
        .rst_n (rst_n),

        .s_tdata (fsm_tdata),
        .s_tvalid (fsm_tvalid),
        .s_tready (fsm_tready),

        .m_tdata (m_axis_tdata),
        .m_tvalid (m_axis_tvalid),
        .m_tready (m_axis_tready)
    );


endmodule
