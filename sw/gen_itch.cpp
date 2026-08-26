// write synthetic MoldUDP64 framed packet file for the board to DMA in
/*
g++ -O2 -o gen_itch gen_itch.cpp
./gen_itch itch_data.bin
./gen_itch itch_data.bin 20000 12345
*/

#include <cstdio>
#include <cstdint>
#include <cstring>
#include "itch_messages.hpp"
#include <cstdlib>
#include <vector>
#include "book_model.hpp"

static constexpr size_t MAX_LIVE_ORDERS = 300;
static constexpr uint32_t PRICE_BASE = 1230000;
static constexpr uint32_t PRICE_SPAN = 20;
static constexpr int MSGS_PER_PACKET = 50;

static std::vector<uint64_t> live_refs;

static void forget_ref(uint64_t ref){
    for (size_t i = 0; i < live_refs.size(); i++){
        if (ref == live_refs[i]){
            // swap w last elem + pop
            live_refs[i] = live_refs.back();
            live_refs.pop_back();
            return;
        }
    }
}

static size_t build_random_stream(FILE *f, int num_msgs, unsigned int seed){
    BookModel model;
    live_refs.clear();
    uint64_t next_ref = 1;
    uint64_t timestamp = 1000;
    uint64_t sequence = 1;
    size_t total = 0;

    srand(seed);

    uint8_t packet[MOLD_HEADER_LEN + MSGS_PER_PACKET * (2 + ORDER_ADD_LEN)];

    int written = 0;
    while (written < num_msgs){
        int in_packet = MSGS_PER_PACKET;
        if (num_msgs - written < in_packet) in_packet = num_msgs - written;

        size_t n = MOLD_HEADER_LEN;
        build_mold_header(&packet[0], sequence, (uint16_t)in_packet);

        for (int m = 0; m < in_packet; m++){
            int x = rand() % 100;
            if (live_refs.empty()) x = 0;
            else if (x < 50 && live_refs.size() >= MAX_LIVE_ORDERS) x = 50;

            if (x < 50){
                OrderAdd a;
                a.stock_locate = 1;
                a.tracking_num = 0;
                a.timestamp = timestamp++;
                a.order_ref_num = next_ref++;
                a.is_buy = (rand() % 2) == 0;
                a.shares = 1 + (rand() % 1000);
                a.stock = "AAPL";
                if (a.is_buy) a.price = PRICE_BASE - 1 - (rand() % PRICE_SPAN);
                else          a.price = PRICE_BASE + (rand() % PRICE_SPAN);

                uint8_t buf[ORDER_ADD_LEN];
                build_order_add(buf, a);
                n += build_mold_msg(&packet[n], buf, ORDER_ADD_LEN);

                model.add_order(a.order_ref_num, a.is_buy, a.price, a.shares);
                live_refs.push_back(a.order_ref_num);
            }
            else if (x < 75){
                uint64_t ref = live_refs[rand() % live_refs.size()];
                OrderExecuted e;
                e.stock_locate = 1;
                e.tracking_num = 0;
                e.timestamp = timestamp++;
                e.order_ref_num = ref;
                e.shares = 1 + (rand() % 1200);
                e.match_num = 0;

                uint8_t buf[ORDER_EXECUTED_LEN];
                build_order_executed(buf, e);
                n += build_mold_msg(&packet[n], buf, ORDER_EXECUTED_LEN);

                model.exc_order(ref, e.shares);
                if (!model.has_order(ref)){
                    forget_ref(ref);
                }
            }
            else if (x < 90){
                uint64_t ref = live_refs[rand() % live_refs.size()];
                OrderDelete d;
                d.stock_locate = 1;
                d.tracking_num = 0;
                d.timestamp = timestamp++;
                d.order_ref_num = ref;

                uint8_t buf[ORDER_DELETE_LEN];
                build_order_delete(buf, d);
                n += build_mold_msg(&packet[n], buf, ORDER_DELETE_LEN);

                model.del_order(ref);
                forget_ref(ref);
            }
            else{
                uint64_t ref = next_ref + 1000000;
                OrderDelete d;
                d.stock_locate = 1;
                d.tracking_num = 0;
                d.timestamp = timestamp++;
                d.order_ref_num = ref;

                uint8_t buf[ORDER_DELETE_LEN];
                build_order_delete(buf, d);
                n += build_mold_msg(&packet[n], buf, ORDER_DELETE_LEN);

                model.del_order(ref);
            }
            written++;
        }

        fwrite(packet, 1, n, f);
        total += n;
        sequence += in_packet;
    }

    FILE *expected = fopen("expected.txt", "w");
    if (expected == nullptr){
        std::printf("FAILED: unable to open expected.txt\n");
        return 0;
    }

    fprintf(expected, "best_bid_price %u\n", model.best_bid_price());
    fprintf(expected, "best_bid_shares %u\n", model.best_bid_shares());
    fprintf(expected, "best_ask_price %u\n", model.best_ask_price());
    fprintf(expected, "best_ask_shares %u\n", model.best_ask_shares());
    fprintf(expected, "spread %u\n", model.spread());
    fprintf(expected, "miss_count %u\n", model.get_miss_count());
    fprintf(expected, "packet_count %d\n", (num_msgs + MSGS_PER_PACKET - 1) / MSGS_PER_PACKET);
    fclose(expected);

    return total;
}

int main(int argc, char **argv){
    const char *path = (argc>1)? argv[1] : "itch_data.bin";

    if (argc > 2){
        int num_msgs = atoi(argv[2]);
        unsigned int seed = (argc>3)? (unsigned int)atoi(argv[3]) : 12345;

        FILE *out = fopen(path, "wb");
        if (out == nullptr){
            std::printf("FAILED: unable to open %s to write\n", path);
            return 1;
        }

        size_t bytes = build_random_stream(out, num_msgs, seed);
        fclose(out);

        std::printf("SUCCESS: wrote %zu B to %s\n", bytes, path);
        return 0;
    }

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
    if (f == nullptr){
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