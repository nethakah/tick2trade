/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off UNUSEDPARAM */
`default_nettype none

module trade_signal
    import msg_pkg::*;
(
    input logic clk,
    input logic rst_n,

    // orderbook
    input logic[31:0] best_bid_price,
    input logic[31:0] best_bid_shares,
    input logic[31:0] best_ask_price,
    input logic[31:0] best_ask_shares,
    input logic book_valid, // pulse

    // cfg_* configuration ports (to be written in software over AXI4-lite)
    input logic cfg_armed, // 1=go, 0=killswitch
    input logic cfg_side, // buy=1 or sell=0
    input logic[31:0] cfg_trigger_price, // at this price or better
    input logic[31:0] cfg_order_shares, // size of preloaded order
    input logic[31:0] cfg_spread_max, // only if market isn't messy (== ask - bid)
    input logic[31:0] cfg_size_min, //  only if enough shares

    // preloaded order (push what we pull from cfg_* from software)
    output logic order_fire, // pulse
    output logic order_side,
    output logic[31:0] order_price,
    output logic[31:0] order_shares,

    // status (readable over axi4-lite for software)
    output logic[31:0] spread,
    output logic[31:0] fire_count
);


// make sure there is actually a market right now (not at baseline values)
logic market_valid;
always_comb begin
    if (best_bid_price == '0 || best_ask_price == 32'hFFFFFFFF) begin
        market_valid = '0;
    end
    else begin
        market_valid = 1'b1;
    end
end 

// has the market hit the price we wanted (inputted from software via cfg_*)
// buying = take the ask so we bring the ask down to our limit or below
// selling = take the bid so we bring the bid up to our limit or above
logic price_ok;
always_comb begin
    if (cfg_side) begin // buy
        price_ok = (best_ask_price <= cfg_trigger_price);
    end 
    else begin // sell
        price_ok = (best_bid_price >= cfg_trigger_price);
    end 
end

// is there sufficient size (shares) sitting to fill our buy/sell
logic size_ok;
always_comb begin
    if (cfg_side) begin // buy so check what is offered
        size_ok = (best_ask_shares >= cfg_size_min);
    end 
    else begin // sell so check what is bid for
        size_ok = (best_bid_shares >= cfg_size_min);
    end
end

// is the market stable enough to trade into (depends on spread)
logic spread_ok;
always_comb begin
    if (market_valid) begin
        spread = best_ask_price - best_bid_price;
    end
    else begin
        spread = 32'hFFFFFFFF;
    end 

    spread_ok = (spread <= cfg_spread_max);
end

// check all conditions met
logic conditions_ok;
always_comb begin
    if (!cfg_armed || !market_valid || !price_ok || !size_ok || !spread_ok) begin
        conditions_ok = '0;
    end 
    else begin
        conditions_ok = 1'b1;
    end
end

// release preloaded order (NO COMPUTING JUST GRABBING FROM cfg_* at the right time)
always_ff @(posedge clk) begin
    if (!rst_n) begin
        order_price <= '0;
        order_shares <= '0;
        order_side <= '0;
        order_fire <= '0;
        fire_count <= '0;
    end
    else begin
        order_fire <= '0; // pulse
        // fire when book updates, not continuously (works because book_valid is a pulse)
        if (book_valid && conditions_ok) begin
            order_fire <= 1'b1;
            order_side <= cfg_side;
            
            // willing to pay up to cfg_trigger_price with up to cfg_order_shares
            // if market is better, we dont change the order - we still use our limit price because we are willing to pay up to it
            order_price <= cfg_trigger_price;
            order_shares <= cfg_order_shares;
            fire_count <= fire_count + 32'd1;
        end
    end
end

// correctness assertions
`ifdef SIM

    assume property( // i.e. REQUIRES()
        @(posedge clk) disable iff (!rst_n)
        market_valid |-> (best_ask_price > best_bid_price)
    ) else $error("order book has a crossed or locked market (bid >= ask so it should've processed)");

    assert property(
        @(posedge clk) disable iff (!rst_n)
        order_fire |=> !order_fire // if order_fire, then must be !order_fire next cycle
    )
    else $error("order_fire held for >1 cycle");
    assert property(
        @(posedge clk) disable iff (!rst_n)
        order_fire |-> $past(cfg_armed) // if order_fire, then must be armed last cycle
    )   
    else $error("order_fire occurred while disarmed");
    assert property(
        @(posedge clk) disable iff (!rst_n)
        order_fire |-> $past(market_valid) // if order_fire, then must be valid last cycle
    )
    else $error("fired on invalid market");
    assert property(
        @(posedge clk) disable iff (!rst_n)
        order_fire |-> $past(book_valid)
    )
    else $error("fired without book update");
    assert property(
        @(posedge clk) disable iff (!rst_n)
        order_fire |-> (order_shares == $past(cfg_order_shares))
    )
    else $error("order size doesn't match preloaded value");
    assert property(
        @(posedge clk) disable iff (!rst_n)
        order_fire |-> (order_price == $past(cfg_trigger_price))
    )
    else $error("order price doesn't match preloaded value");
    assert property(
        @(posedge clk) disable iff (!rst_n)
        order_fire |-> (fire_count == $past(fire_count) + 32'd1)
    )
    else $error("fire_count didn't increment on order_fire");
    assert property(
        @(posedge clk) disable iff (!rst_n)
        (fire_count != $past(fire_count)) |-> order_fire
    )
    else $error("fire_count incremented without a fire");
`endif

endmodule
