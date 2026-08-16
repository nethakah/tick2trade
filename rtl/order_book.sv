module order_book
    import msg_pkg::*;
(
    input logic clk,
    input logic rst_n,

    // slave - parsed msgs incoming from itch_parser.sv
    input msg_t s_axis_tdata,
    input logic s_axis_tvalid,
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

    bucket_t book_mem[NUM_BUCKETS]; // L3 table - 1 read for whole bucket
    level_t bid_levels[NUM_LEVELS]; // L2 bids
    level_t ask_levels[NUM_LEVELS]; // L2 asks

    msg_t curr_msg;
    logic[BOOK_ADDR_WIDTH-1:0] curr_bucket; // hashed bucket index
    bucket_t bucket_data; // bucket we read

    // THE PARALLEL COMPARES
    logic[ENTRIES_PER_BUCKET-1:0] match_hit; // indices hold whether they are the order we want
    logic[ENTRIES_PER_BUCKET-1:0] free_hit; // indices hold whether they are empty
    always_comb begin
        for (int i = 0; i < ENTRIES_PER_BUCKET; i++) begin
            match_hit[i] = bucket_data[i].valid && (bucket_data[i].order_ref_num == curr_msg.order_ref_num);
            free_hit[i] = !bucket_data[i].valid;
        end
    end
    logic match_found;
    logic free_found;
    logic[$clog2(ENTRIES_PER_BUCKET)-1:0] match_index;
    logic[$clog2(ENTRIES_PER_BUCKET)-1:0] free_index;
    always_comb begin
        match_found = |match_hit; // OR every bit in match_hit
        free_found = |free_hit; // OR every bit in free_hit

        match_index = '0;
        free_index = '0;
        for (int i = 0; i < ENTRIES_PER_BUCKET; i++) begin
            if (match_hit[i]) begin
                match_index = $clog2(ENTRIES_PER_BUCKET)'(i); 
                // size cast to that width
            end
            if (free_hit[i]) begin
                free_index = $clog2(ENTRIES_PER_BUCKET)'(i); 
                // size cast to that width
            end
        end
    end

    // accept msg when idle (processing is 3 cycles)
    assign s_axis_tready = (state == IDLE);

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state <= IDLE;
            overflow_count <= '0;
            miss_count <= '0;
            book_valid <= '0;
        end
        else begin
            book_valid <= '0; // 1cycle pulse

            case (state)
                IDLE: begin
                    if (s_axis_tvalid && s_axis_tready) begin
                        curr_msg <= s_axis_tdata;
                        curr_bucket <= hash_order_ref(s_axis_tdata.order_ref_num);
                        state <= READ_BUCKET;
                    end
                end
                READ_BUCKET: begin
                    // sync read (address applied this cycle, data gets to bucket_data at next edge)
                    // causes inferred BRAM/URAM instead of LUTRAM
                    bucket_data <= book_mem[curr_bucket];
                    state <= UPDATE;
                end
                UPDATE: begin
                    state <= IDLE;
                    case (curr_msg.msg_type)
                        MSG_ADD: begin
                            // find free slot in bucket + write order into it
                            // check every slot in parallel
                            if (free_found) begin
                                book_mem[curr_bucket][free_index].valid <= 1'b1;
                                book_mem[curr_bucket][free_index].is_buy <= curr_msg.is_buy;
                                book_mem[curr_bucket][free_index].stock_locate <= curr_msg.stock_locate;
                                book_mem[curr_bucket][free_index].price <= curr_msg.price;
                                book_mem[curr_bucket][free_index].shares <= curr_msg.shares;
                                book_mem[curr_bucket][free_index].order_ref_num <= curr_msg.order_ref_num;
                            end
                            else begin
                                overflow_count <= overflow_count + 32'd1;
                            end
                        end

                        MSG_EXC: begin
                            // shares filled against an existing order
                            // subtract them; if fully filled then free the slot
                            if (match_found) begin
                                if (curr_msg.shares >= bucket_data[match_index].shares) begin
                                    book_mem[curr_bucket][match_index].valid <= '0;
                                end
                                else begin
                                    book_mem[curr_bucket][match_index].shares <= 
                                    bucket_data[match_index].shares - curr_msg.shares;
                                end
                            end
                            else begin
                                miss_count <= miss_count + 32'd1;
                            end
                        end

                        MSG_DEL: begin
                            // order is cancelled entirely - clear valid
                            if (match_found) begin
                                book_mem[curr_bucket][match_index].valid <= '0;
                            end
                            else begin
                                miss_count <= miss_count + 32'd1;
                            end
                        end

                        default: ;
                    endcase
                end

                default: begin
                    state <= IDLE;
                end
            endcase

        end
    end

endmodule
