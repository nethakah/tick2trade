module order_book
    import msg_pkg::*;
(
    input logic clk,
    input logic rst_n,

    // slave - parsed msgs incoming from itch_parser.sv
    input msg_t s_axis_tdata,
    input logic s_axis_tvaid,
    output logic s_axis_tready,

    // top of book (updated when a msg changes it)
    output logic[31:0] best_bid_price,
    output logic[31:0] best_bid_shares,
    output logic[31:0] best_ask_price,
    output logic[31:0] best_ask_shares,
    output logic book_valid,

    // status updates
    output logic[31:0] overflow_count, // no free slot in bucket to Add
    output logic[31:0] miss_count // executed/del referenced an order not in the book
);
    typedef enum logic[1:0]{
        IDLE = 2'd0,
        READ_BUCKET = 2'd1,
        UPDATE = 2'd2
    } book_state_enum;
    book_state_enum state;

    bucket_t book_mem[NUM_BUCKETS]; // the L3 table - 1 read for whole bucket

    msg_t curr_msg;
    logic[BOOK_ADDR_WIDTH-1:0] curr_bucket; // hashed bucket index
    bucket_t bucket_data; // bucket we read

    assign s_axis_tready = (state == IDLE);

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state <= IDLE;
            overflow_count <= '0;
            miss_count <= '0;
            book_valid <= '0;
        end
    end

endmodule