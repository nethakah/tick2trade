/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off UNUSEDPARAM */

module async_fifo #(
    parameter int DATA_WIDTH = 8,
    parameter int DEPTH = 16

)(
    // write
    input logic w_clk,
    input logic w_rst_n,
    input logic w_enbl,
    input logic[DATA_WIDTH-1:0] w_data,
    output logic full,
    
    // read
    input logic r_clk,
    input logic r_rst_n,
    input logic r_enbl,
    output logic[DATA_WIDTH-1:0] r_data,
    output logic empty
);
    localparam int ADDR_WIDTH = $clog2(DEPTH);

    logic[DATA_WIDTH-1:0] mem[DEPTH];

    // binary pointers for incremeneting and addressing memory
    logic[ADDR_WIDTH:0] w_ptr_bnry;
    logic[ADDR_WIDTH:0] r_ptr_bnry;

    // gray coded copies to cross clock domains (1 bit change per increment)
    logic[ADDR_WIDTH:0] w_ptr_gray;
    logic[ADDR_WIDTH:0] r_ptr_gray;

    logic[ADDR_WIDTH:0] r_ptr_gray_q1;
    logic[ADDR_WIDTH:0] r_ptr_gray_q2;
    logic[ADDR_WIDTH:0] w_ptr_gray_q1;
    logic[ADDR_WIDTH:0] w_ptr_gray_q2;

    logic[ADDR_WIDTH:0] w_ptr_bnry_next;
    logic[ADDR_WIDTH:0] r_ptr_bnry_next;

    logic do_write;
    logic do_read;

    /****************** 
    WRITING SIDE (w_clk) 
    ******************/
    
    assign do_write = w_enbl && !full;
    assign w_ptr_bnry_next = do_write ? (w_ptr_bnry+1) : w_ptr_bnry; 
    // when we write, pointer goes to next slot, else it stays

    always_ff @(posedge w_clk) begin
        if (do_write) begin
            mem[w_ptr_bnry[ADDR_WIDTH-1:0]] <= w_data;
        end
    end

    always_ff @(posedge w_clk) begin
    // derive from *_next to update on same edge w same logical count
        if (!w_rst_n) begin
            w_ptr_bnry <= '0;
            w_ptr_gray <= '0;
        end else begin  
            w_ptr_bnry <= w_ptr_bnry_next;
            w_ptr_gray <= (w_ptr_bnry_next >> 1) ^ w_ptr_bnry_next;
        end
    end

    // Two ff synchronizer (give metastability a full clock to decay)
    // READ ptr entering the WRITE domain
    // 1. sample signal from foreign clock (metastable)
    // 2. sample it a full w_clk after (only graycoded)
    always_ff @(posedge w_clk) begin
    // sync2 samples 1 clock later than sync1 (basically no change of metastability)
        if (!w_rst_n) begin
            r_ptr_gray_q1 <= '0;
            r_ptr_gray_q2 <= '0;
        end else begin
            r_ptr_gray_q1 <= r_ptr_gray;
            r_ptr_gray_q2 <= r_ptr_gray_q1;
        end
    end

    // Check full: W lapped R (on same slot but different lap)
    // gray = bin ^ (bin >> 1):
    // so flipping bin's MSB flips gray's MSB, flips next bit down, and every lower gray bit is not touched
    // so r_ptr_gray_lapped = r_ptr_gray with TOP 2 gray bits inverted
    logic[ADDR_WIDTH:0] r_ptr_gray_lapped;
    assign r_ptr_gray_lapped = {~r_ptr_gray_q2[ADDR_WIDTH:ADDR_WIDTH-1],
                                r_ptr_gray_q2[ADDR_WIDTH-2:0]};
    assign full = (w_ptr_gray == r_ptr_gray_lapped);

    /****************** 
    READING SIDE (r_clk) (this is pre similar to W side)
    ******************/ 

    assign do_read = r_enbl && !empty;
    assign r_ptr_bnry_next = do_read ? (r_ptr_bnry+1) : r_ptr_bnry; 

    // ASYNC read here
    // we infer LUTRAM here which is negligible at 16 depth
    // might need to consider sync if depth grows
    // index w lower bits only bc MSB is a lap counter
    // this is FWFT (First-Word Fall-Thru) elaborated in /docs/log.md
    assign r_data = mem[r_ptr_bnry[ADDR_WIDTH-1:0]];

    always_ff @(posedge r_clk) begin
        if (!r_rst_n) begin
            r_ptr_bnry <= '0;
            r_ptr_gray <= '0;
        end else begin
            r_ptr_bnry <= r_ptr_bnry_next;
            r_ptr_gray <= (r_ptr_bnry_next >> 1) ^ r_ptr_bnry_next;
        end
    end

    // Two ff sync
    // WRITE ptr entering READ domain
    always_ff @(posedge r_clk) begin
        if (!r_rst_n) begin
            w_ptr_gray_q1 <= '0;
            w_ptr_gray_q2 <= '0;
        end else begin
            w_ptr_gray_q1 <= w_ptr_gray;
            w_ptr_gray_q2 <= w_ptr_gray_q1;
        end
    end

    // Check empty: same slot same lap
    assign empty = (r_ptr_gray == w_ptr_gray_q2);

    `ifdef SIM
        assert property(
            @(posedge w_clk) disable iff (!w_rst_n)
            !(full && empty)
        ) else $error("empty and full at the same time");
        assert property(
            @(posedge w_clk) disable iff (!w_rst_n)
            (full && w_enbl) |-> !do_write
        ) else $error("wrote while full");
        assert property(
            @(posedge r_clk) disable iff (!r_rst_n)
            (empty && r_enbl) |-> !do_read
        ) else $error("read while empty");
    `endif

endmodule
