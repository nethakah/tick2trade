#ifndef ITCH_MESSAGES_H
#define ITCH_MESSAGES_H

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

struct AddOrder 
{
    uint16_t stock_locate; // @1 book array index
    uint16_t tracking_num; // @3 internal for Nasdaq (parser ignores this)
    uint64_t timestamp; // @5
    uint64_t order_ref_num; // @11
    bool is_buy; // @19
    uint32_t shares; // @20
    const char* stock; // @24
    uint32_t price; // @32
};

static constexpr size_t ADD_ORDER_LEN = 36; // (compile time)

static inline void build_add_order(uint8_t *dest, const AddOrder &msg)
{
    dest[0] = 'A';

    write_u16_bigendian(&dest[1], msg.stock_locate);
    write_u16_bigendian(&dest[3], msg.tracking_num);
    write_u48_bigendian(&dest[5], msg.timestamp);
    write_u64_bigendian(&dest[11], msg.order_ref_num);

    dest[19] = msg.is_buy ? 'B' : 'S';

    write_u32_bigendian(&dest[20], msg.shares);

    std::memset(&dest[24], ' ', 8);
    size_t len = std::strlen(msg.stock);
    if (len > 8) {
        len = 8;
    }
    std::memcpy(&dest[24], msg.stock, len);

    write_u32_bigendian(&dest[32], msg.price);
}

static inline uint64_t ticker_to_u64(const char *symbol)
{
    uint8_t padded[8];
    std::memset(padded, ' ', 8);

    size_t len = std::strlen(symbol);
    if (len > 8) {
        len = 8;
    }
    std::memcpy(padded, symbol, len);

    uint64_t res = 0;
    for (int i = 0; i < 8; i++) {
        res = (res<<8) | padded[i];
    }

    return res;
}

#endif // ITCH_MESSAGES_H