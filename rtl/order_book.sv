/* 
L3 = book memory = every order keyed by ref numbers, hashed to a bucket
L2 = bid/ask levels = shares split up by price levels derived from L3 so we 
can find best bid without scanning all the orders per message

Add is 4 cycles (bc carries its price)
Exc/Del is 5 cycles (bc no price in the message)
*/

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
    output logic[31:0] miss_count, // executed/del referenced an order not in the book
    output logic [31:0] level_collision_count // 2 prices hashed to same level slot
);
    logic[31:0] top_bid_price;
    logic[31:0] top_bid_shares;
    logic[31:0] top_ask_price;
    logic[31:0] top_ask_shares;

    assign best_bid_price = top_bid_price;
    assign best_bid_shares = top_bid_shares;
    assign best_ask_price = top_ask_price;
    assign best_ask_shares = top_ask_shares;
    
    typedef enum logic[2:0]{
        IDLE = 3'd0,
        READ_BUCKET = 3'd1,
        FIND_ORDER = 3'd2, // if A, reads level here nad updates
        READ_LEVEL = 3'd3, // for E/D - read the level after learning the price
        UPDATE = 3'd4, // modify both L3 and L2
        RESCAN = 3'd5 // for when top level fully empties
    } book_state_enum;
    book_state_enum state;
    // ADD (4): IDLE -> READ_BUCKET -> FIND_ORDER -> UPDATE
    // E/D (5): IDLE -> READ_BUCKET -> FIND_ORDER -> READ_LEVEL -> UPDATE

    bucket_t book_mem[NUM_BUCKETS]; // L3 table - 1 read for whole bucket
    level_t bid_levels[NUM_LEVELS]; // L2 bids
    level_t ask_levels[NUM_LEVELS]; // L2 asks

    msg_t curr_msg;
    logic[BOOK_ADDR_WIDTH-1:0] curr_bucket; // hashed bucket index
    bucket_t bucket_data; // bucket we read

    // L2 registers (latched in FIND_ORDER)
    level_t level_data; // level we read
    logic[LEVEL_ADDR_WIDTH-1:0] curr_level; // hashed level index
    logic curr_is_buy; // which side's array to use
    logic[31:0] curr_price; // price of order being changed
    logic[31:0] shares_removed; // num of shares that leave this level
    logic order_left; // did order leave book entirely

    // store its price on the level to verify hash landed on right one
    logic level_match;
    assign level_match = level_data.valid && (level_data.price == curr_price);

    // for rescanning when top of book level empties
    logic[LEVEL_ADDR_WIDTH-1:0] rescan_index;
    logic rescan_is_buy;
    logic[31:0] rescan_best_price;
    logic[31:0] rescan_best_shares;

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

    // accept msg when idle (processing spans 4-5 cycles)
    assign s_axis_tready = (state == IDLE);

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state <= IDLE;
            overflow_count <= '0;
            miss_count <= '0;
            level_collision_count <= '0;
            book_valid <= '0;

            top_bid_shares <= '0;
            top_ask_shares <= '0;
            top_bid_price <= '0; // new bids win by being higher so start at 0
            top_ask_price <= 32'hFFFFFFFF; // new asks win by being lower so we start at max 32b num
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
                    state <= FIND_ORDER;
                end

                FIND_ORDER: begin
                    // match_hit/free_hit settled off bucket_data, so searching is zero cycles
                    if (curr_msg.msg_type == MSG_ADD) begin
                        // Add has its price embedded so level address is known already
                        curr_price <= curr_msg.price;
                        curr_is_buy <= curr_msg.is_buy;
                        curr_level <= hash_price(curr_msg.price);
                        level_data <= curr_msg.is_buy ?
                                      bid_levels[hash_price(curr_msg.price)] : ask_levels[hash_price(curr_msg.price)];
                        state <= UPDATE;
                    end
                    else if (match_found) begin
                        // level read costs +1 cycle since E/D dont carry price
                        curr_price <= bucket_data[match_index].price;
                        curr_is_buy <= bucket_data[match_index].is_buy;
                        curr_level <= hash_price(bucket_data[match_index].price);

                        // if its DEL, or fully filled execute, then remove whole order
                        if (curr_msg.msg_type == MSG_DEL || curr_msg.shares >= bucket_data[match_index].shares) begin
                            shares_removed <= bucket_data[match_index].shares;
                            order_left <= 1'b1;
                        end
                        else begin
                            // partial fill
                            shares_removed <= curr_msg.shares;
                            order_left <= '0;
                        end
                        state <= READ_LEVEL;
                    end
                    else begin
                        // order not in the book or Add was lost in a seq gap
                        miss_count <= miss_count + 32'd1;
                        state <= IDLE;
                    end
                end 

                READ_LEVEL: begin
                    level_data <= curr_is_buy ? bid_levels[curr_level] : ask_levels[curr_level];
                    state <= UPDATE;
                end

                // every msg writes both things in 1 cycle here
                UPDATE: begin
                    state <= IDLE;
                    case (curr_msg.msg_type)

                        MSG_ADD: begin
                            // find free slot in bucket + write order into it
                            // check every slot in parallel
                            if (free_found) begin
                                // L3 - write the order
                                book_mem[curr_bucket][free_index].valid <= 1'b1;
                                book_mem[curr_bucket][free_index].is_buy <= curr_msg.is_buy;
                                book_mem[curr_bucket][free_index].stock_locate <= curr_msg.stock_locate;
                                book_mem[curr_bucket][free_index].price <= curr_msg.price;
                                book_mem[curr_bucket][free_index].shares <= curr_msg.shares;
                                book_mem[curr_bucket][free_index].order_ref_num <= curr_msg.order_ref_num;
                            
                                // L2 - add shares at price
                                if (level_match) begin // level exists
                                    if (curr_is_buy) begin
                                        bid_levels[curr_level].total_shares <= level_data.total_shares + curr_msg.shares;
                                        bid_levels[curr_level].order_count <= level_data.order_count + 16'd1;
                                    end
                                    else begin
                                        ask_levels[curr_level].total_shares <= level_data.total_shares + curr_msg.shares;
                                        ask_levels[curr_level].order_count <= level_data.order_count + 16'd1;
                                    end

                                    book_valid <= 1'b1;
                                end
                                else if (!level_data.valid) begin
                                    // empty slot so create the level
                                    if (curr_is_buy) begin
                                        bid_levels[curr_level].price <= curr_msg.price;
                                        bid_levels[curr_level].total_shares <= curr_msg.shares;
                                        bid_levels[curr_level].order_count <= 16'd1;
                                        bid_levels[curr_level].valid <= 1'b1;
                                    end
                                    else begin
                                        ask_levels[curr_level].price <= curr_msg.price;
                                        ask_levels[curr_level].total_shares <= curr_msg.shares;
                                        ask_levels[curr_level].order_count <= 16'd1;
                                        ask_levels[curr_level].valid <= 1'b1;
                                    end

                                    book_valid <= 1'b1;
                                end
                                else begin
                                    // slot has a different price, so ladder is undersized or hash is cooked
                                    level_collision_count <= level_collision_count + 32'd1;
                                end

                                // Top of Book
                                if (curr_is_buy) begin
                                    if (curr_msg.price == top_bid_price) begin
                                        top_bid_shares <= level_data.total_shares + curr_msg.shares;
                                    end
                                    else if (curr_msg.price > top_bid_price) begin
                                        top_bid_price <= curr_msg.price;
                                        if (level_match) begin
                                            top_bid_shares <= level_data.total_shares + curr_msg.shares; 
                                        end
                                        else begin
                                            top_bid_shares <= curr_msg.shares; 
                                        end
                                    end
                                end

                                else begin
                                    if (curr_msg.price == top_ask_price) begin
                                        top_ask_shares <= level_data.total_shares + curr_msg.shares;
                                    end
                                    else if (curr_msg.price < top_ask_price) begin
                                        top_ask_price <= curr_msg.price;
                                        if (level_match) begin
                                            top_ask_shares <= level_data.total_shares + curr_msg.shares;
                                        end
                                        else begin
                                            top_ask_shares <= curr_msg.shares; 
                                        end
                                    end
                                end
                            end
                            else begin
                                overflow_count <= overflow_count + 32'd1;
                            end
                        end

                        // L2 work identical for both E/D
                        MSG_DEL, MSG_EXC: begin
                            // L3
                            if (!order_left) begin
                                book_mem[curr_bucket][match_index].shares <= bucket_data[match_index].shares - shares_removed;
                            end
                            else begin
                                book_mem[curr_bucket][match_index].valid <= '0;
                            end

                            // L2
                            if (level_match) begin
                               if (level_data.order_count==16'd1 && order_left) begin
                               // last order at curr price so level should disappear
                                    if (curr_is_buy) begin
                                        bid_levels[curr_level].valid <= '0;
                                    end
                                    else begin
                                        ask_levels[curr_level].valid <= '0;
                                    end

                                    // RESCAN CASE - if the level we're removing was top of book
                                    if (curr_is_buy && curr_price==top_bid_price) begin
                                        rescan_is_buy <= 1'b1;
                                        rescan_index <= '0;
                                        rescan_best_shares <= '0;
                                        rescan_best_price <= '0;
                                        state <= RESCAN;
                                    end
                                    else if (!curr_is_buy && curr_price==top_ask_price) begin
                                        rescan_is_buy <= '0;
                                        rescan_index <= '0;
                                        rescan_best_shares <= '0;
                                        rescan_best_price <= 32'hFFFFFFFF;
                                        state <= RESCAN;
                                    end
                                end
                                else begin
                                    // level not removed but just reduced by some shares
                                    if (curr_is_buy) begin
                                        bid_levels[curr_level].total_shares <= level_data.total_shares - shares_removed;
                                        
                                        if (order_left) begin
                                            bid_levels[curr_level].order_count <= level_data.order_count-16'd1;
                                        end
                                        else begin
                                            bid_levels[curr_level].order_count <= level_data.order_count;
                                        end

                                        // if top of book then top share count moves too
                                        if (curr_price == top_bid_price) begin
                                            top_bid_shares <= level_data.total_shares - shares_removed;
                                        end
                                    end

                                    else begin
                                        ask_levels[curr_level].total_shares <= level_data.total_shares - shares_removed;
                                        
                                        if (order_left) begin
                                            ask_levels[curr_level].order_count <= level_data.order_count-16'd1;
                                        end
                                        else begin
                                            ask_levels[curr_level].order_count <= level_data.order_count;
                                        end

                                        // if top of book again top share count moves too
                                        if (curr_price == top_ask_price) begin
                                            top_ask_shares <= level_data.total_shares - shares_removed;
                                        end
                                    end
                               end

                               book_valid <= 1'b1;
                            end
                            else begin
                                // order exists in L3 but the price doesn't have a level
                                level_collision_count <= level_collision_count + 32'd1;
                            end
                        end

                        default: ;
                    endcase
                end

                RESCAN: begin
                    if (rescan_is_buy) begin // higher bid replaces
                        if (bid_levels[rescan_index].valid && (bid_levels[rescan_index].price > rescan_best_price)) begin
                            rescan_best_shares <= bid_levels[rescan_index].total_shares;
                            rescan_best_price <= bid_levels[rescan_index].price;
                        end
                    end
                    else begin // lower ask replaces
                        if (ask_levels[rescan_index].valid && (ask_levels[rescan_index].price < rescan_best_price)) begin
                            rescan_best_shares <= ask_levels[rescan_index].total_shares;
                            rescan_best_price <= ask_levels[rescan_index].price;
                        end
                    end

                    if (rescan_index == LEVEL_ADDR_WIDTH'(NUM_LEVELS-1)) begin // cast to right size
                        if (rescan_is_buy) begin
                            top_bid_shares <= rescan_best_shares;
                            top_bid_price <= rescan_best_price;
                        end
                        else begin
                            top_ask_shares <= rescan_best_shares;
                            top_ask_price <= rescan_best_price;
                        end

                        state <= IDLE;
                        book_valid <= 1'b1;
                    end

                    else begin // increment scan
                        rescan_index <= rescan_index + 1'b1;
                    end
                end

                default: begin
                    state <= IDLE;
                end
            endcase
        end
    end

    `ifdef SIM
        assert property(
            @(posedge clk) disable iff (!rst_n)
            (best_bid_price != '0 && best_ask_price != 32'hFFFFFFFF) |-> (best_ask_price > best_bid_price)
        ) else $error("book published a crossed market (bid is at or above ask - not possible)");
        assert property(
            @(posedge clk) disable iff (!rst_n)
            !$stable(best_ask_price) |-> book_valid
        ) else $error("ask changed w/o book update");
        assert property(
            @(posedge clk) disable iff (!rst_n)
            !$stable(best_bid_price) |-> book_valid
        ) else $error("bid changed w/o book update");
        assert property(
            @(posedge clk) disable iff (!rst_n)
            miss_count >= $past(miss_count)
        ) else $error("miss_count decremented somehow");
    `endif

endmodule
