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

    assign s_axis_tready = 1'b1; 
    // always 1byte/cycle so we don't need to stall for now (streaming FSM)

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state <= READ_TYPE;
            byte_index <= 6'd0;
            m_axis_tvalid <= 1'b0;
        end else begin
            m_axis_tvalid <= 1'b0;

            if (s_axis_tvalid) begin
                case (state)
                    READ_TYPE: begin
                        if (msg_length(s_axis_tdata) != 6'd0) begin // recognized type
                            m_axis_tdata.msg_type <= decode_type(s_axis_tdata);
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
                            6'd1: m_axis_tdata.stock_locate[15:8] <= s_axis_tdata;
                            6'd2: m_axis_tdata.stock_locate[7:0] <= s_axis_tdata;

                            // Tracking num @3 (2B)
                            6'd3: ;
                            6'd4: ;
                            
                            // Timestamp @5 (6B)
                            6'd5: m_axis_tdata.timestamp[47:40] <= s_axis_tdata;
                            6'd6: m_axis_tdata.timestamp[39:32] <= s_axis_tdata;
                            6'd7: m_axis_tdata.timestamp[31:24] <= s_axis_tdata;
                            6'd8: m_axis_tdata.timestamp[23:16] <= s_axis_tdata;
                            6'd9: m_axis_tdata.timestamp[15:8] <= s_axis_tdata;
                            6'd10: m_axis_tdata.timestamp[7:0] <= s_axis_tdata;

                            // Order ref num @11 (8B)
                            6'd11: m_axis_tdata.order_ref_num[63:56] <= s_axis_tdata;
                            6'd12: m_axis_tdata.order_ref_num[55:48] <= s_axis_tdata;
                            6'd13: m_axis_tdata.order_ref_num[47:40] <= s_axis_tdata;
                            6'd14: m_axis_tdata.order_ref_num[39:32] <= s_axis_tdata;
                            6'd15: m_axis_tdata.order_ref_num[31:24] <= s_axis_tdata;
                            6'd16: m_axis_tdata.order_ref_num[23:16] <= s_axis_tdata;
                            6'd17: m_axis_tdata.order_ref_num[15:8] <= s_axis_tdata;
                            6'd18: m_axis_tdata.order_ref_num[7:0] <= s_axis_tdata;

                            default: ;
                        endcase

                        case (m_axis_tdata.msg_type)
                            MSG_ADD: begin
                                case (byte_index)
                                    6'd19: m_axis_tdata.is_buy <= (s_axis_tdata == "B");

                                    // Shares @20 (4B)
                                    6'd20: m_axis_tdata.shares[31:24] <= s_axis_tdata;
                                    6'd21: m_axis_tdata.shares[23:16] <= s_axis_tdata;
                                    6'd22: m_axis_tdata.shares[15:8] <= s_axis_tdata;
                                    6'd23: m_axis_tdata.shares[7:0] <= s_axis_tdata;

                                    // Stock @24 (8B)
                                    6'd24: m_axis_tdata.stock[63:56] <= s_axis_tdata;
                                    6'd25: m_axis_tdata.stock[55:48] <= s_axis_tdata;
                                    6'd26: m_axis_tdata.stock[47:40] <= s_axis_tdata;
                                    6'd27: m_axis_tdata.stock[39:32] <= s_axis_tdata;
                                    6'd28: m_axis_tdata.stock[31:24] <= s_axis_tdata;
                                    6'd29: m_axis_tdata.stock[23:16] <= s_axis_tdata;
                                    6'd30: m_axis_tdata.stock[15:8] <= s_axis_tdata;
                                    6'd31: m_axis_tdata.stock[7:0] <= s_axis_tdata;

                                    // Price @32 (4B) (KEY NOTE: 4 IMPLIED DECIMALS (fixed pt))
                                    6'd32: m_axis_tdata.price[31:24] <= s_axis_tdata;
                                    6'd33: m_axis_tdata.price[23:16] <= s_axis_tdata;
                                    6'd34: m_axis_tdata.price[15:8] <= s_axis_tdata;
                                    6'd35: m_axis_tdata.price[7:0] <= s_axis_tdata;

                                    default: ;
                                endcase
                            end

                            MSG_EXC: begin
                                case (byte_index)
                                    6'd19: m_axis_tdata.shares[31:24] <= s_axis_tdata;
                                    6'd20: m_axis_tdata.shares[23:16] <= s_axis_tdata;
                                    6'd21: m_axis_tdata.shares[15:8] <= s_axis_tdata;
                                    6'd22: m_axis_tdata.shares[7:0] <= s_axis_tdata;

                                    // Match num @23 (8B) (the unique day execution ID)
                                    6'd23: m_axis_tdata.match_num[63:56] <= s_axis_tdata;
                                    6'd24: m_axis_tdata.match_num[55:48] <= s_axis_tdata;
                                    6'd25: m_axis_tdata.match_num[47:40] <= s_axis_tdata;
                                    6'd26: m_axis_tdata.match_num[39:32] <= s_axis_tdata;
                                    6'd27: m_axis_tdata.match_num[31:24] <= s_axis_tdata;
                                    6'd28: m_axis_tdata.match_num[23:16] <= s_axis_tdata;
                                    6'd29: m_axis_tdata.match_num[15:8] <= s_axis_tdata;
                                    6'd30: m_axis_tdata.match_num[7:0] <= s_axis_tdata;

                                    default: ;
                                endcase
                            end

                            MSG_DEL: ;
                            MSG_NONE: ;
                            default: ;
                        endcase // END OF MESSAGE! 
                        
                        // curr_len = total bytes
                        if (byte_index == curr_len - 6'd1) begin
                            m_axis_tvalid <= 1'b1;
                            state <= READ_TYPE;
                        end else begin
                            byte_index <= byte_index + 6'd1;
                        end
                    end
                endcase
            end
        end
    end


endmodule
