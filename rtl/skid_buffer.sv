/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off UNUSEDPARAM */

module skid_buffer #(
    parameter int DATA_WIDTH = 32
)(
    input logic clk,
    input logic rst_n,

    input logic [DATA_WIDTH-1:0] s_tdata,
    input logic s_tvalid,
    output logic s_tready,

    output logic [DATA_WIDTH-1:0] m_tdata,
    output logic m_tvalid,
    input logic m_tready
);
    typedef enum logic [1:0] {
        SLOTS0 = 2'd0, // nothing held so nothing to present downstream
        SLOTS1 = 2'd1, // 1 item held
        SLOTS2 = 2'd2 // both slots held so upstream needs to stall
    } occupancy_enum;

    occupancy_enum occupancy;

    logic [DATA_WIDTH-1:0] spare_data;

    logic in_xfer; // item is entering this cycle
    logic out_xfer; // item is leaving this cycle

    assign in_xfer = s_tvalid && s_tready;
    assign out_xfer = m_tvalid && m_tready;

    assign s_tready = (occupancy != SLOTS2); // there's room unless 2 slots full
    assign m_tvalid = (occupancy != SLOTS0); // smth to offer unless empty

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            occupancy <= SLOTS0;
            // resetting m_tdata and spare_data is a waste
        end else begin
            case (occupancy)
                SLOTS0: begin
                    if (in_xfer) begin
                        m_tdata <= s_tdata; // straight to front slot
                        occupancy <= SLOTS1;
                    end
                end

                SLOTS1: begin
                    if (in_xfer && out_xfer) begin
                        // 1 leaves + 1 arrives = count unchanged
                        // new item goes straight to front slot
                        m_tdata <= s_tdata;
                    end else if (in_xfer) begin
                        // 1 arrives + 0 leaves = consumer stalls
                        spare_data <= s_tdata;
                        occupancy <= SLOTS2;
                    end else if (out_xfer) begin
                        // 1 leaves + 0 arrives = drain to empty
                        occupancy <= SLOTS0;
                    end
                end

                SLOTS2: begin
                    // s_tready low here so no in_xfer possible (upstream alr stalled)
                    if (out_xfer) begin
                        m_tdata <= spare_data;
                        occupancy <= SLOTS1;
                    end
                end
            
                default: begin
                    occupancy <= SLOTS0;
                end
            endcase
        end
    end

    `ifdef SIM
        assert property(
            @(posedge clk) disable iff (!rst_n)
            (occupancy == SLOTS2) |-> !s_tready
        ) else $error("accepted input while full");
        assert property(
            @(posedge clk) disable iff (!rst_n)
            (occupancy == SLOTS0) |-> !m_tvalid
        ) else $error("offered data while empty");
    `endif

endmodule
