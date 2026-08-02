package msg_pkg;

    typedef enum logic [3:0] {
        MSG_NONE = 4'd0, // no msg or unrecognized type
        MSG_ADD = 4'd1, // add order (A)
        MSG_EXC = 4'd2, // order execute (E)
        MSG_DEL = 4'd3 // order delete (D)
    } msgtype_enum;

    typedef struct packed {
        // common header (@N byte offset)
        msgtype_enum msg_type;      // @0 add/execute/delete
        logic [15:0] stock_locate;  // @1 book array index 
        logic [47:0] timestamp;     // @5 ns since 12am
        logic [63:0] order_ref_num; // @11 book key

        // add order body
        logic is_buy;               // @19 1='B'=Buy, 0='S'=Sell
        logic [31:0] shares;        // @20 shares to add ('A') / executed shares ('E')
        logic [63:0] stock;         // @24 8-char ASCII symbol ('A')
        logic [31:0] price;         // @32 raw fixed-point (4 decimals implied)
        
        // misc
        logic [63:0] match_num;     // @23 match number ('E') (day unique to identify this specific execution)
    } msg_t;

endpackage
