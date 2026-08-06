package msg_pkg;

    typedef enum logic [3:0] {
        MSG_NONE = 4'd0, // no msg or unrecognized type
        MSG_ADD = 4'd1, // add order (A)
        MSG_EXC = 4'd2, // order execute (E)
        MSG_DEL = 4'd3 // order delete (D)
    } msgtype_enum;

    typedef struct packed {
        logic [26:0] rsvd0;         // reserved for C++ padding
        msgtype_enum msg_type;      // @0 add/execute/delete
        
        logic [15:0] rsvd1;         // reserved for C++ padding
        logic [15:0] stock_locate;  // @1 book array index 
        
        logic [15:0] rsvd2;     // reserved for C++ padding
        logic [47:0] timestamp;     // @5 ns since 12am
        
        logic [63:0] order_ref_num; // @11 book key

        logic is_buy;               // @19 1='B'=Buy, 0='S'=Sell
        logic [31:0] shares;        // @20 shares to add ('A') / executed shares ('E')
        logic [63:0] stock;         // @24 8-char ASCII symbol ('A')
        logic [31:0] price;         // @32 raw fixed-point (4 decimals implied)

        logic [63:0] match_num;     // @23 match number ('E') (day unique to identify this specific execution)
    } msg_t; // 384b with padding (so every field starts on a 32-bit boundary)

    function automatic logic [5:0] msg_length( // bytes
    input logic [7:0] type_byte); 
        case (type_byte)
            // offset of last field + len of last field
            "A": msg_length = 6'd36;
            "E": msg_length = 6'd31;
            "D": msg_length = 6'd19;
            default: msg_length = 6'd0; 
        endcase
    endfunction

    function automatic msgtype_enum decode_type(
    input logic [7:0] type_byte); 
        case (type_byte)
            "A": decode_type = MSG_ADD;
            "E": decode_type = MSG_EXC;
            "D": decode_type = MSG_DEL;
            default: decode_type = MSG_NONE;
        endcase
    endfunction
    
endpackage

