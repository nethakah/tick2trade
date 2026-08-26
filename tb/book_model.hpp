#ifndef BOOK_MODEL_HPP
#define BOOK_MODEL_HPP

#include <cstdint>
#include <map>
#include "contracts.hpp"

class BookModel{
    private:
        struct Order{
            bool is_buy;
            uint32_t price;
            uint32_t shares;
        };

        // <key, value>, variable_name;
        std::map<uint64_t, Order> orders;
        std::map<uint32_t, uint32_t> bid_levels;
        std::map<uint32_t, uint32_t> ask_levels;

        uint32_t miss_count = 0;

        void remove_from_level(bool is_buy, uint32_t price, uint32_t shares){
            std::map<uint32_t, uint32_t> &levels = is_buy? bid_levels : ask_levels;

            auto i = levels.find(price);
            ASSERT(i != levels.end());
            ASSERT(i->second >= shares);

            i->second -= shares;
            if (i->second == 0){
                levels.erase(i);
            }
        }
    public:
        static constexpr uint32_t BID_EMPTY = 0;
        static constexpr uint32_t ASK_EMPTY = 0xFFFFFFFF;

        bool has_order(uint64_t ref_num){
            return (orders.count(ref_num) > 0);
        }

        void add_order(
            uint64_t ref_num,
            bool is_buy,
            uint32_t price,
            uint32_t shares
        ){
            REQUIRES(shares > 0);
            REQUIRES(orders.count(ref_num) == 0);

            // build the struct
            orders[ref_num] = Order{is_buy, price, shares};

            if (is_buy){
                bid_levels[price] += shares;
            }
            else{
                ask_levels[price] += shares;
            }
        }

        void exc_order(uint64_t ref_num, uint32_t executed_shares){
            auto i = orders.find(ref_num);
            if (i == orders.end()){
                miss_count++;
                return;
            }

            Order &o = i->second; // value half of key,value pair

            if (executed_shares >= o.shares){
                remove_from_level(o.is_buy, o.price, o.shares);
                orders.erase(i);
            }
            else{
                remove_from_level(o.is_buy, o.price, executed_shares);
                o.shares -= executed_shares;
            }
        }

        void del_order(uint64_t ref_num){
            auto i = orders.find(ref_num);
            if (i == orders.end()){
                miss_count++;
                return;
            }

            Order &o = i->second;
            remove_from_level(o.is_buy, o.price, o.shares);
            orders.erase(i);
        }

        uint32_t best_bid_price(){
            if (bid_levels.empty()){
                return BID_EMPTY;
            }
            return bid_levels.rbegin()->first; // last to first
        }
        uint32_t best_ask_price(){
            if (ask_levels.empty()){
                return ASK_EMPTY;
            }
            return ask_levels.begin()->first;
        }
        uint32_t best_bid_shares(){
            if (bid_levels.empty()){
                return 0;
            }
            return bid_levels.rbegin()->second; // last to first
        }
        uint32_t best_ask_shares(){
            if (ask_levels.empty()){
                return 0;
            }
            return ask_levels.begin()->second;
        }

        uint32_t spread(){
            if (best_bid_price() == BID_EMPTY){
                return 0xFFFFFFFF;
            }
            if (best_ask_price() == ASK_EMPTY){
                return 0xFFFFFFFF;
            }
            
            return best_ask_price() - best_bid_price();
        }

        uint32_t get_miss_count(){
            return miss_count;
        }
};

#endif // BOOK_MODEL_HPP
