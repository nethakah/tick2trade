package msg_pkg;

    typedef enum logic[3:0]{
        MSG_NONE = 4'd0, // no msg or unrecognized type
        MSG_ADD = 4'd1, // add order (A)
        MSG_EXC = 4'd2, // order execute (E)
        MSG_DEL = 4'd3 // order delete (D)
    } msgtype_enum;

    typedef struct packed{
    // declaration order is the bit order for ALL message types (wont match wire format from NASDAQ)
    // commented is wire format @byte location, actual parser handed down format is the order of the fields
        logic[26:0] rsvd0;         // reserved for C++ padding
        logic is_buy;               // @19 1='B'=Buy, 0='S'=Sell
        msgtype_enum msg_type;      // @0 add/execute/delete
        
        logic[15:0] rsvd1;         // reserved for C++ padding
        logic[15:0] stock_locate;  // @1 book array index 
        
        logic[15:0] rsvd2;         // reserved for C++ padding
        logic[47:0] timestamp;     // @5 ns since 12am
        
        logic[63:0] order_ref_num; // @11 book key
        logic[31:0] shares;        // @20 shares to add ('A') / executed shares ('E')
        logic[63:0] stock;         // @24 8-char ASCII symbol ('A')
        logic[31:0] price;         // @32 raw fixed-point (4 decimals implied)
        logic[63:0] match_num;     // @23 match number ('E') (day unique to identify this specific execution)
    } msg_t; // 384b with padding (so every field starts on a 32-bit boundary)

    function automatic logic[5:0] msg_length( // bytes
    input logic[7:0] type_byte); 
        case (type_byte)
            // offset of last field + len of last field
            "A": msg_length = 6'd36;
            "E": msg_length = 6'd31;
            "D": msg_length = 6'd19;
            default: msg_length = 6'd0; 
        endcase
    endfunction

    function automatic msgtype_enum decode_type(
    input logic[7:0] type_byte); 
        case (type_byte)
            "A": decode_type = MSG_ADD;
            "E": decode_type = MSG_EXC;
            "D": decode_type = MSG_DEL;
            default: decode_type = MSG_NONE;
        endcase
    endfunction

    typedef struct packed{
        logic[13:0] rsvd;      // reserved for C++ padding
        logic valid;            // slot occupied
        logic is_buy;
        logic[15:0] stock_locate;
        logic[31:0] price;
        logic[31:0] shares;
        logic[63:0] order_ref_num;
    } order_entry_t; // 160b with padding (so every field starts on 32-bit boundary)
    
    localparam int ENTRIES_PER_BUCKET = 4; // each bucket has this many entries in 1 memory word (parallel compared)
    localparam int BOOK_ADRR_WIDTH = 12; // 4096 buckets
    localparam int NUM_BUCKETS = 1 << BOOK_ADDR_WIDTH;
    localparam int BOOK_CAP = NUM_BUCKETS * ENTRIES_PER_BUCKET; // 16384 live orders

    typedef order_entry_t[ENTRIES_PER_BUCKET-1:0] bucket_t; // 640b per bucket

endpackage

