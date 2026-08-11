#ifndef ITCH_MESSAGES_HPP
#define ITCH_MESSAGES_HPP

#include <cstdint>
#include <cstring>
#include <cstddef>

// 2 byte fields (stock locate, tracking num)
static inline void write_u16_bigendian(uint8_t *dest, uint16_t value)
{
    dest[0] = (uint8_t)(value>>8);
    dest[1] = (uint8_t)(value);
}

// 4 byte fields (shares, price)
static inline void write_u32_bigendian(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)(value>>24);
    dest[1] = (uint8_t)(value>>16);
    dest[2] = (uint8_t)(value>>8);
    dest[3] = (uint8_t)(value);
}

// 6 byte fields (timestamp)
static inline void write_u48_bigendian(uint8_t *dest, uint64_t value)
{   
    // IGNORE 16 BITS (48 bits r held in a 64-bit variable here)
    // (since obv theres no 6byte number we can use)
    dest[0] = (uint8_t)(value>>40);
    dest[1] = (uint8_t)(value>>32);
    dest[2] = (uint8_t)(value>>24);
    dest[3] = (uint8_t)(value>>16);
    dest[4] = (uint8_t)(value>>8);
    dest[5] = (uint8_t)(value);
}

// 8  byte fields (order ref num, match num)
static inline void write_u64_bigendian(uint8_t *dest, uint64_t value)
{
    dest[0] = (uint8_t)(value>>56);
    dest[1] = (uint8_t)(value>>48);
    dest[2] = (uint8_t)(value>>40);
    dest[3] = (uint8_t)(value>>32);
    dest[4] = (uint8_t)(value>>24);
    dest[5] = (uint8_t)(value>>16);
    dest[6] = (uint8_t)(value>>8);
    dest[7] = (uint8_t)(value);
}

static inline uint64_t ticker_to_u64(const char *symbol)
{
    uint8_t padded[8];
    std::memset(padded, ' ', 8);

    size_t len = std::strlen(symbol);
    if (len > 8){
        len = 8;
    }
    std::memcpy(padded, symbol, len);

    uint64_t res = 0;
    for (int i = 0; i < 8; i++){
        res = (res<<8) | padded[i];
    }

    return res;
}

// bytes 0-18 are the same w A,E,D so we can fix one edit easily
static inline void build_common_header(
    uint8_t *dest,
    char type_char,
    uint16_t stock_locate,
    uint16_t tracking_num,
    uint64_t timestamp,
    uint64_t order_ref_num
){
    dest[0] = (uint8_t)type_char;
    write_u16_bigendian(&dest[1], stock_locate);
    write_u16_bigendian(&dest[3], tracking_num);
    write_u48_bigendian(&dest[5], timestamp);
    write_u64_bigendian(&dest[11], order_ref_num);
}

// ORDER ADD
static constexpr size_t ORDER_ADD_LEN = 36;
struct OrderAdd{
    uint16_t stock_locate; // @1 book array index
    uint16_t tracking_num; // @3 internal for Nasdaq (parser ignores this)
    uint64_t timestamp; // @5
    uint64_t order_ref_num; // @11
    bool is_buy; // @19
    uint32_t shares; // @20
    const char* stock; // @24
    uint32_t price; // @32
};
static inline void build_order_add(uint8_t *dest, const OrderAdd &msg)
{
    // common fields
    build_common_header(dest, 'A',
                        msg.stock_locate, msg.tracking_num,
                        msg.timestamp, msg.order_ref_num);

    // rest of fields:

    dest[19] = msg.is_buy ? 'B' : 'S'; //@19
    write_u32_bigendian(&dest[20], msg.shares); //@20

    std::memset(&dest[24], ' ', 8); //@24
    size_t len = std::strlen(msg.stock);
    if (len > 8) len = 8;
    std::memcpy(&dest[24], msg.stock, len);

    write_u32_bigendian(&dest[32], msg.price); //@32
}

// ORDER EXECUTED
static constexpr size_t ORDER_EXECUTED_LEN = 31;
struct OrderExecuted{
    uint16_t stock_locate; //@1
    uint16_t tracking_num; //@3
    uint64_t timestamp; //@5
    uint64_t order_ref_num; //@11
    uint32_t shares; //@19
    uint64_t match_num; //@23
};
static inline void build_order_executed(uint8_t *dest, const OrderExecuted &msg)
{
    // common fields
    build_common_header(dest, 'E', 
                        msg.stock_locate, msg.tracking_num,
                        msg.timestamp, msg.order_ref_num);  
    // rest of fields
    write_u32_bigendian(&dest[19], msg.shares);
    write_u64_bigendian(&dest[23], msg.match_num);
}

// ORDER DELETE
static constexpr size_t ORDER_DELETE_LEN = 19;
struct OrderDelete{
    uint16_t stock_locate; //@1
    uint16_t tracking_num; //@3
    uint64_t timestamp; //@5
    uint64_t order_ref_num; //@11
};
static inline void build_order_delete(uint8_t *dest, const OrderDelete &msg)
{
    // common fields
    build_common_header(dest, 'D',
                        msg.stock_locate, msg.tracking_num,
                        msg.timestamp, msg.order_ref_num);
    // no other fields
}

#endif // ITCH_MESSAGES_HPP