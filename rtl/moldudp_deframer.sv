/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off UNUSEDPARAM */
`default_nettype none

module moldudp_deframer
    import msg_pkg::*;
(
    input logic clk,
    input logic rst_n,

    // axi4-stream slave
    input logic[7:0] s_axis_tdata, // raw packet bytes
    input logic s_axis_tvalid,
    input logic s_axis_tlast, // final byte of 1 UDP packet
    output logic s_axis_tready,

    // axi4-stream master
    output logic[7:0] m_axis_tdata, // bare ITCH bytes to send to parser
    output logic m_axis_tvalid,
    input logic m_axis_tready,

    // status
    output logic[63:0] sequence_num, // spec field is 8 bytes
    output logic gap_detected, // 1 cycle pulse
    output logic[31:0] gap_count, // total msgs missed
    output logic[31:0] packet_count, // total packets seen
    output logic packet_error
);
    
    typedef enum logic[2:0]{
        READ_SESSION = 3'd0, // 10 header bytes; discarded
        READ_SEQUENCE = 3'd1, // 8 bytes; sequence_num
        READ_COUNT = 3'd2, // 2 bytes; msg_count - gap check here
        READ_LEN = 3'd3, // 2 bytes; curr_msg_len
        PASS_DATA = 3'd4 // push bytes (curr_msg_len many) to parser
    } state_enum;
    state_enum state;

    logic[3:0] byte_count; // pos in current field
    logic[15:0] msg_count; // msgs left in this packet
    logic[15:0] curr_msg_len; // bytes left in curr msg
    logic[63:0] expected_sequence; // prev seq + prev count
    logic first_packet; // suppress the gap check on the first packet (unnecessary/bug)

    // stall on our own pending output (dont emulate bug-04)
    assign s_axis_tready = !m_axis_tvalid;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state <= READ_SESSION;
            byte_count <= '0;
            gap_detected <= '0;
            gap_count <= '0;
            packet_count <= '0;
            first_packet <= 1'b1;
            m_axis_tvalid <= '0;
            packet_error <= '0;
        end
        else begin
            gap_detected <= 1'b0;

            if (m_axis_tready) begin
                m_axis_tvalid <= 1'b0; // clear when accepted; not unconditionally
            end

            if (s_axis_tvalid && s_axis_tready) begin // FSM advances on handshaked complete transfer
                case (state)
                    // header field 1: session 10 bytes
                    READ_SESSION: begin
                        if (byte_count == 4'd9) begin // bytes 0-9 = 10 = session len
                            byte_count <= 4'd0;
                            state <= READ_SEQUENCE;
                        end
                        else begin
                            byte_count <= byte_count + 4'd1;
                        end
                    end

                    // header field 2: sequence num 8 bytes
                    READ_SEQUENCE: begin
                        sequence_num <= {sequence_num[55:0], s_axis_tdata}; 
                        // drop the top byte (keep low 56 and add new byte to bottom & shift up)
                        if (byte_count == 4'd7) begin // bytes 0-7 = 8 = seq len
                            byte_count <= 4'd0;
                            state <= READ_COUNT;
                        end
                        else begin
                            byte_count <= byte_count + 4'd1;
                        end
                    end

                    // header field 3: itch-message count 2 bytes
                    READ_COUNT: begin
                        msg_count <= {msg_count[7:0], s_axis_tdata};

                        if (byte_count == 4'd1) begin // bytes 0-1 = 2 = count len
                            byte_count <= 4'd0;

                            // GAP CHECK HERE!!!
                            // next packet's seq must equal the prev packet's seq + its msg count
                            if (!first_packet && sequence_num!=expected_sequence) begin
                                gap_detected <= 1'b1;
                                gap_count <= gap_count + sequence_num[31:0] - expected_sequence[31:0];
                            end

                            // msg_count low byte still on wire this cycle
                            // build full count fom high byte + s_axis_tdata
                            expected_sequence <= sequence_num + {48'd0, msg_count[7:0], s_axis_tdata};
                            packet_count <= packet_count + 32'd1;
                            first_packet <= 1'b0;

                            state <= READ_LEN;
                        end
                        else begin
                            byte_count <= byte_count + 4'd1;
                        end
                    end

                    // Per msg field (length - 2 bytes BIG ENDIAN)
                    // this is for every ITCH msg in the given packet
                    READ_LEN: begin
                        curr_msg_len <= {curr_msg_len[7:0], s_axis_tdata};

                        if (byte_count == 4'd1) begin
                            byte_count <= 4'd0;
                            state <= PASS_DATA;
                        end
                        else begin
                            byte_count <= byte_count + 4'd1;
                        end
                    end

                    // Per msg field (msg data - curr_msg_len bytes)
                    // this is basically raw ITCH msg so we'll drive master port to reach parser
                    PASS_DATA: begin
                        m_axis_tdata <= s_axis_tdata;
                        m_axis_tvalid <= 1'b1;

                        // if 1 remaining byte, then this is the last one (curr one is being consumed rn)
                        if (curr_msg_len == 16'd1) begin
                            if (msg_count == 16'd1) begin // final msg in pckaet
                                if (!s_axis_tlast) begin
                                    packet_error <= 1'b1;
                                end
                                state <= READ_SESSION; // next packet header
                            end
                            else begin // not final msg in packet
                                if (s_axis_tlast) begin
                                    packet_error <= 1'b1; // packet ended early so must have msgs missing
                                end
                                msg_count <= msg_count - 16'd1;
                                state <= READ_LEN; // next msg length
                            end
                        end 
                        else begin
                            curr_msg_len <= curr_msg_len - 16'd1;
                        end
                    end

                    default: ;
                endcase
            end
        end
    end

    `ifdef SIM
        assert property(
            @(posedge clk) disable iff (!rst_n)
            m_axis_tvalid |-> $past(state == PASS_DATA)
        ) else $error("used a byte outside PASS_DATA");
        assert property(
            @(posedge clk) disable iff (!rst_n)
            (m_axis_tvalid && !m_axis_tready) |=> m_axis_tvalid
        ) else $error("AXI error (tvalid dropped before we accepted from prev cycle handshake)");
        assert property(
            @(posedge clk) disable iff (!rst_n)
            gap_count >= $past(gap_count)
        ) else $error("gap_count decremented");
    `endif

endmodule
`default_nettype wire
