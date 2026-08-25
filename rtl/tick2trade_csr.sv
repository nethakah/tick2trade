/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off UNUSEDPARAM */

module tick2trade_csr #(
    parameter int ADDR_WIDTH = 8 
    // AXI-lite is byte addressed so 8 bits covers 64 regs
)(
    input logic clk,
    input logic rst_n,

    // AW - Write Address
    input logic[ADDR_WIDTH-1:0] s_axi_awaddr,
    input logic s_axi_awvalid,
    output logic s_axi_awready,

    // W - Write data channel
    input logic[31:0] s_axi_wdata,
    input logic[3:0] s_axi_wstrb, // 1 bit per byte of the 32b wdata - byte enable (write the byte if bit=1)
    input logic s_axi_wvalid,
    output logic s_axi_wready,

    // B - Write response channel
    output logic[1:0] s_axi_bresp,
    output logic s_axi_bvalid,
    input logic s_axi_bready,

    // AR - Read address channel
    input logic[ADDR_WIDTH-1:0] s_axi_araddr,
    input logic s_axi_arvalid,
    output logic s_axi_arready,

    // R - Read data channel
    output logic[31:0] s_axi_rdata,
    output logic[1:0] s_axi_rresp,
    output logic s_axi_rvalid,
    input logic s_axi_rready,

    // Cfg output to trade_signal and order_book
    output logic cfg_armed,
    output logic cfg_side,
    output logic[31:0] cfg_trigger_price,
    output logic[31:0] cfg_order_shares,
    output logic[31:0] cfg_spread_max,
    output logic[31:0] cfg_size_min,
    output logic[15:0] cfg_stock_locate,

    // Status in from pipeline
    input logic[31:0] best_bid_price,
    input logic[31:0] best_ask_price,
    input logic[31:0] best_bid_shares,
    input logic[31:0] best_ask_shares,
    input logic[31:0] spread,
    input logic[31:0] fire_count,
    input logic[31:0] packet_count,
    input logic[31:0] gap_count,
    input logic[31:0] miss_count,
    input logic[31:0] overflow_count,
    input logic[31:0] level_collision_count,
    input logic[31:0] fire_latency_cycles,
    input logic[31:0] fire_latency_min,
    input logic[31:0] fire_latency_max
);

    // register map
    localparam logic[5:0] REG_CONTROL = 6'h00; // RW; ([0]=armed) ([1]=side(1=buy))
    localparam logic[5:0] REG_TRIGGER_PRICE = 6'h01; // RW; fire when market hits this
    localparam logic[5:0] REG_ORDER_SHARES = 6'h02; // RW; size of preloaded order
    localparam logic[5:0] REG_SPREAD_MAX = 6'h03; // RW; max spread
    localparam logic[5:0] REG_SIZE_MIN = 6'h04; // RW; require this much resting size
    localparam logic[5:0] REG_STOCK_LOCATE = 6'h05; // RW; what symbol
    localparam logic[5:0] REG_BEST_BID_PRICE = 6'h08; // R;
    localparam logic[5:0] REG_BEST_BID_SHARES = 6'h09; // R;
    localparam logic[5:0] REG_BEST_ASK_PRICE = 6'h0A; // R;
    localparam logic[5:0] REG_BEST_ASK_SHARES = 6'h0B; // R;
    localparam logic[5:0] REG_SPREAD = 6'h0C; // R;
    localparam logic[5:0] REG_FIRE_COUNT = 6'h0D; // R; orders released
    localparam logic[5:0] REG_PACKET_COUNT = 6'h0E; // R; packets deframed
    localparam logic[5:0] REG_GAP_COUNT = 6'h0F; // R; packets lost to gaps
    localparam logic[5:0] REG_MISS_COUNT = 6'h10; // R; E/D for an unknown order
    localparam logic[5:0] REG_OVERFLOW_COUNT = 6'h11; // R; L3 bucket full when trying to insert
    localparam logic[5:0] REG_LEVEL_COLLISION = 6'h12; // R; 2 prices hashed to 1 L2 slot
    localparam logic[5:0] REG_FIRE_LATENCY = 6'h13; // R; cycles, most recent fire
    localparam logic[5:0] REG_LATENCY_MIN = 6'h14; // R; cycles, best latency we got
    localparam logic[5:0] REG_LATENCY_MAX = 6'h15; // R; cycles, worst latency we got
    localparam logic[1:0] RESP_OKAY = 2'b00; // response code (tell master whether it worked)

    logic[5:0] write_index;
    logic[5:0] read_index;
    assign write_index = s_axi_awaddr[7:2]; // drop byte-select bits
    assign read_index = s_axi_araddr[7:2]; // drop byte-select bits

    /*
    write path - remember A/AW are independent (master can send addr whenever relative to data)
    */

    logic aw_held;
    logic w_held;
    logic[5:0] aw_index;
    logic[31:0] w_data;

    // accept when we arent holding one
    assign s_axi_awready = !aw_held;
    assign s_axi_wready = !w_held;
    // both = good to go so commit
    logic write_commit;
    assign write_commit = aw_held && w_held;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            aw_held <= '0;
            w_held <= '0;
            aw_index <= '0;
            w_data <= '0;
            s_axi_bvalid <= '0;

            cfg_armed <= '0;
            cfg_side <= '0;
            cfg_trigger_price <= '0;
            cfg_order_shares <= '0;
            cfg_spread_max <= '0;
            cfg_size_min <= '0;
            cfg_stock_locate <= '0;
        end
        else begin
            if (s_axi_awvalid && s_axi_awready) begin
                aw_held <= 1'b1;
                aw_index <= write_index;
            end 
            if (s_axi_wvalid && s_axi_wready) begin
                w_held <= 1'b1;
                w_data <= s_axi_wdata;
            end
            if (write_commit && !s_axi_bvalid) begin
                case (aw_index)
                    REG_CONTROL: begin
                        cfg_armed <= w_data[0];
                        cfg_side <= w_data[1];
                    end
                    REG_TRIGGER_PRICE: begin
                        cfg_trigger_price <= w_data;
                    end
                    REG_ORDER_SHARES: begin
                        cfg_order_shares <= w_data;
                    end
                    REG_SPREAD_MAX: begin
                        cfg_spread_max <= w_data;
                    end
                    REG_SIZE_MIN: begin
                        cfg_size_min <= w_data;
                    end
                    REG_STOCK_LOCATE: begin
                        cfg_stock_locate <= w_data[15:0];
                    end
                    default: ;
                endcase

                aw_held <= '0;
                w_held <= '0;
                s_axi_bvalid <= 1'b1;
            end
            if (s_axi_bvalid && s_axi_bready) begin // response accepted
                s_axi_bvalid <= '0;
            end
        end
    end
    assign s_axi_bresp = RESP_OKAY;

    /*
    read path - AR arrives, look up val, drive R
    */

    logic[5:0] ar_index;
    assign s_axi_arready = !s_axi_rvalid; // accept read addr when not holding a resp

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            ar_index <= '0;
            s_axi_rvalid <= '0;
            s_axi_rdata <= '0;
        end
        else begin
            if (s_axi_arvalid && s_axi_arready) begin
                ar_index <= read_index;
                case (read_index)
                    REG_CONTROL: begin
                        s_axi_rdata <= {30'd0, cfg_side, cfg_armed};
                    end
                    REG_TRIGGER_PRICE: begin
                        s_axi_rdata <= cfg_trigger_price;
                    end
                    REG_ORDER_SHARES: begin
                        s_axi_rdata <= cfg_order_shares;
                    end
                    REG_SPREAD_MAX: begin
                        s_axi_rdata <= cfg_spread_max;
                    end
                    REG_SIZE_MIN: begin
                        s_axi_rdata <= cfg_size_min;
                    end
                    REG_STOCK_LOCATE: begin
                        s_axi_rdata <= {16'd0, cfg_stock_locate};
                    end
                    REG_BEST_BID_PRICE: begin
                        s_axi_rdata <= best_bid_price;
                    end
                    REG_BEST_ASK_PRICE: begin
                        s_axi_rdata <= best_ask_price;
                    end
                    REG_BEST_BID_SHARES: begin
                        s_axi_rdata <= best_bid_shares;
                    end
                    REG_BEST_ASK_SHARES: begin
                        s_axi_rdata <= best_ask_shares;
                    end
                    REG_SPREAD: begin
                        s_axi_rdata <= spread;
                    end
                    REG_FIRE_COUNT: begin
                        s_axi_rdata <= fire_count;
                    end
                    REG_PACKET_COUNT: begin
                        s_axi_rdata <= packet_count;
                    end
                    REG_GAP_COUNT: begin
                        s_axi_rdata <= gap_count;
                    end
                    REG_MISS_COUNT: begin
                        s_axi_rdata <= miss_count;
                    end
                    REG_OVERFLOW_COUNT: begin
                        s_axi_rdata <= overflow_count;
                    end
                    REG_LEVEL_COLLISION: begin
                        s_axi_rdata <= level_collision_count;
                    end
                    REG_FIRE_LATENCY: begin
                        s_axi_rdata <= fire_latency_cycles;
                    end
                    REG_LATENCY_MIN: begin
                        s_axi_rdata <= fire_latency_min;
                    end
                    REG_LATENCY_MAX: begin
                        s_axi_rdata <= fire_latency_max;
                    end
                    default: begin
                        s_axi_rdata <= '0;
                    end
                endcase
                s_axi_rvalid <= 1'b1;
            end
            if (s_axi_rvalid && s_axi_rready) begin // data accepted
                s_axi_rvalid <= '0;
            end
        end
    end
    assign s_axi_rresp = RESP_OKAY;

`ifdef SIM
    assert property(
        @(posedge clk) disable iff (!rst_n)
        (s_axi_rvalid && !s_axi_rready) |=> $stable(s_axi_rdata)
    ) else $error("rdata altered while pending read");
    assert property(
        @(posedge clk) disable iff (!rst_n)
        (s_axi_bvalid && !s_axi_bready) |=> s_axi_bvalid
    ) else $error("bvalid dropped too early (not accepted by master)");
    assert property(
        @(posedge clk) disable iff (!rst_n)
        $rose(cfg_armed) |-> $past(write_commit)
    ) else $error("armed without a write");
    assert property(
        @(posedge clk) disable iff (!rst_n)
        (s_axi_rvalid && !s_axi_rready) |=> s_axi_rvalid
    ) else $error("rvalid dropped too early (not accepted by master)");
`endif

endmodule
