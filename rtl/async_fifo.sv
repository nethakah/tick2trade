// Needed to wire the core clock and the AXI-DMA clock.

module async_fifo #(
    parameter int DATA_WIDTH = 8,
    parameter int DEPTH = 16

)(
    // write
    input logic w_clk,
    input logic w_rst_n,
    input logic w_enbl,
    input logic [DATA_WIDTH-1:0] w_data,
    output logic full,
    
    // read
    input logic r_clk,
    input logic r_rst_n,
    input logic r_enbl,
    output logic [DATA_WIDTH-1:0] r_data,
    output logic empty
);
    localparam int ADDR_WIDTH = $clog2(DEPTH);

    logic [DATA_WIDTH-1:0] mem [DEPTH];

    // binary pointers for incremeneting and addressing memory
    logic [ADDR_WIDTH:0] w_ptr_bnry;
    logic [ADDR_WIDTH:0] r_ptr_bnry;

    // gray coded copies to cross clock domains (1 bit change per increment)
    logic [ADDR_WIDTH:0] w_ptr_gray;
    logic [ADDR_WIDTH:0] r_ptr_gray;

    // sync'd copies of OTHER domain's pointer after 2 flops
    logic [ADDR_WIDTH:0] r_ptr_gray_sync;
    logic [ADDR_WIDTH:0] w_ptr_gray_sync;

    logic [ADDR_WIDTH:0] w_ptr_bnry_next;
    logic [ADDR_WIDTH:0] r_ptr_bnry_next;
    logic do_write;
    logic do_read;

    // WRITING SIDE OF FIFO //
    
    assign do_write = w_enbl && !full;
    assign w_ptr_bnry_next = do_write ? (w_ptr_bnry+1) : w_ptr_bnry; 
    // when we write, pointer goes to next slot, else it stays

    always_ff @(posedge w_clk) begin
        if (do_write) begin
            mem[w_ptr_bnry[ADDR_WIDTH-1:0]] <= w_data;
        end
    end

    always_ff @(posedge w_clk) begin
        if (!w_rst_n) begin
            w_ptr_bnry <= '0;
            w_ptr_gray <= '0;

        end else begin  
            w_ptr_bnry <= w_ptr_bnry_next;
            w_ptr_gray <= (w_ptr_bnry_next >> 1) ^ w_ptr_bnry_next;
        end
    end

    // READING SIDE OF FIFO //



endmodule
