// write synthetic MoldUDP64 framed packet file for the board to DMA in
/*
g++ -O2 -o gen_itch gen_itch.cpp
./gen_itch itch_data.bin
*/

#include <cstdio>
#include <cstdint>
#include <cstring>
#include "itch_messages.hpp"

int main(int argc, char **argv){
    const char *path = (argc>1)? argv[1] : "itch_data.bin";

    OrderAdd bid;
    bid.stock_locate = 1;
    bid.tracking_num = 1;
    bid.timestamp = 1000;
    bid.order_ref_num = 100;
    bid.is_buy = true;
    bid.shares = 500;
    bid.stock = "AAPL";
    bid.price = 1230000;

    OrderAdd ask;
    ask.stock_locate = 1;
    ask.tracking_num = 2;
    ask.timestamp = 2000;
    ask.order_ref_num = 200;
    ask.is_buy = false;
    ask.shares = 300;
    ask.stock = "AAPL";
    ask.price = 1230500;

    uint8_t itch_bid[ORDER_ADD_LEN];
    uint8_t itch_ask[ORDER_ADD_LEN];
    build_order_add(itch_bid, bid);
    build_order_add(itch_ask, ask);

    uint8_t packet[128];
    size_t n = 0;

    build_mold_header(&packet[0], 1000, 2);
    n = MOLD_HEADER_LEN;
    n += build_mold_msg(&packet[n], itch_bid, ORDER_ADD_LEN);
    n += build_mold_msg(&packet[n], itch_ask, ORDER_ADD_LEN);

    FILE *f = fopen(path, "wb"); // return FILE*; "wb" = write+binary
    if (!f){
        std::printf("FAILED: was unable to open %s for writing\n", path);
        return 1;
    }
    // fwrite (source, item_size, item_count, file)
    fwrite(packet, 1, n, f);

    // fwrite buffers in memory so we need fclose so it reaches disk
    fclose(f);

    std::printf("SUCCESS: wrote %zu bytes to %s\n", n, path);
    return 0;
}