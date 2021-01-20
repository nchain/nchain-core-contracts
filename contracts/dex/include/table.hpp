#pragma once

#include <map>
#include <eosio/eosio.hpp>
#include <eosio/name.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>
#include "dex_const.hpp"
#include "utils.hpp"

namespace dex {

    using namespace eosio;

    typedef name order_type_t;
    typedef name order_side_t;
    typedef name order_status_t;

    namespace order_type {
        static const order_type_t NONE = order_type_t();
        static const order_type_t LIMIT = "limit"_n;
        static const order_type_t MARKET = "market"_n;
        // order_type_t -> index
        static const std::map<order_type_t, uint8_t> ENUM_MAP = {
            {LIMIT, 1},
            {MARKET, 2}
        };

        inline bool is_valid(const order_type_t &value) {
            return ENUM_MAP.count(value);
        }

        inline uint8_t index(const order_type_t &value) {
                if (value == NONE) return 0;
                auto it = ENUM_MAP.find(value);
                CHECK(it != ENUM_MAP.end(), "Invalid order_type=" + value.to_string());
                return it->second;
        }
    }

    namespace order_side {
        static const order_type_t NONE = order_type_t();
        static const order_side_t BUY = "buy"_n;
        static const order_side_t SELL = "sell"_n;
        // name -> index
        static const std::map<order_side_t, uint8_t> ENUM_MAP = {
            {BUY, 1},
            {SELL, 2}
        };
        inline bool is_valid(const order_side_t &value) {
            return ENUM_MAP.count(value);
        }
        inline uint8_t index(const order_side_t &value) {
                if (value == NONE) return 0;
                auto it = ENUM_MAP.find(value);
                CHECK(it != ENUM_MAP.end(), "Invalid order_type=" + value.to_string());
                return it->second;
        }
    }

    namespace order_status {
        static const order_status_t NONE = order_status_t();
        static const order_status_t MATCHABLE = "matchable"_n;
        static const order_status_t COMPLETED = "completed"_n;
        static const order_status_t CANCELED = "canceled"_n;
        // name -> index
        static const std::map<order_status_t, uint8_t> ENUM_MAP = {
            {MATCHABLE,     1},
            {COMPLETED,      2},
            {CANCELED,      3}
        };

        inline uint8_t index(const order_status_t &value) {
                if (value == NONE) return 0;
                auto it = ENUM_MAP.find(value);
                CHECK(it != ENUM_MAP.end(), "Invalid order_status=" + value.to_string());
                return it->second;
        }
    }

    struct DEX_TABLE config {
        name admin;   // admin of this contract, permisions: manage sym_pairs, authorize order
        name payee;   // payee of this contract
        int64_t maker_ratio;
        int64_t taker_ratio;
        uint32_t max_match_count; // the max match count for creating new order,  if 0 will forbid match
        bool     check_order_auth; // check the order must have the authorization by dex admin
    };

    typedef eosio::singleton< "config"_n, config > config_table;

    struct DEX_TABLE global {
        uint64_t order_id    = 0; // the auto-increament id of order
        uint64_t sym_pair_id = 0; // the auto-increament id of symbol pair
        uint64_t deal_item_id = 0; // the auto-increament id of deal item
    };

    typedef eosio::singleton< "global"_n, global > global_table;

    struct global_state: public global {
    public:
        bool changed = false;

        using ptr_t = std::unique_ptr<global_state>;

        static ptr_t make_global(const name &contract) {
            std::shared_ptr<global_table> global_tbl;
            auto ret = std::make_unique<global_state>();
            ret->_global_tbl = std::make_unique<global_table>(contract, contract.value);

            if (ret->_global_tbl->exists()) {
                static_cast<global&>(*ret) = ret->_global_tbl->get();
            }
            return ret;
        }

        inline uint64_t new_auto_inc_id(uint64_t &id) {
            if (id == 0 || id == std::numeric_limits<uint64_t>::max()) {
                id = 1;
            } else {
                id++;
            }
            change();
            return id;
        }

        inline uint64_t new_order_id() {
            return new_auto_inc_id(order_id);
        }

        inline uint64_t new_sym_pair_id() {
            return new_auto_inc_id(sym_pair_id);
        }

        inline uint64_t new_deal_item_id() {
            return new_auto_inc_id(deal_item_id);
        }

        inline void change() {
            changed = true;
        }

        inline void save(const name &payer) {
            if (changed) {
                auto &g = static_cast<global&>(*this);
                _global_tbl->set(g, payer);
                changed = false;
            }
        }
    private:
        std::unique_ptr<global_table> _global_tbl;
    };

    using uint256_t = fixed_bytes<32>;

    static inline uint256_t make_symbols_idx(const extended_symbol &asset_symbol, const extended_symbol &coin_symbol) {

        return uint256_t::make_from_word_sequence<uint64_t>(asset_symbol.get_contract().value, asset_symbol.get_symbol().code().raw(),
            coin_symbol.get_contract().value, coin_symbol.get_symbol().code().raw());
    }

    struct DEX_TABLE symbol_pair_t {
        uint64_t sym_pair_id; // auto-increment
        extended_symbol asset_symbol;
        extended_symbol coin_symbol;
        asset min_asset_quant;
        asset min_coin_quant;
        bool only_accept_coin_fee;
        bool enabled;

        uint64_t primary_key() const { return sym_pair_id; }
        inline uint256_t get_symbols_idx() const { return make_symbols_idx(asset_symbol, coin_symbol); }
    };

    using symbols_idx = indexed_by<"symbolsidx"_n, const_mem_fun<symbol_pair_t, uint256_t,
           &symbol_pair_t::get_symbols_idx>>;

    typedef eosio::multi_index<"sympair"_n, symbol_pair_t, symbols_idx> symbol_pair_table;

    inline static symbol_pair_table make_symbol_pair_table(const name &self) {
        return symbol_pair_table(self, self.value/*scope*/);
    }

    using order_match_idx_key = uint256_t;
    inline static order_match_idx_key make_order_match_idx(uint64_t sym_pair_id, const order_status_t &status,
                                                           const order_side_t &side,
                                                           const order_type_t &type, uint64_t price,
                                                           uint64_t order_id) {
        uint64_t option = uint64_t(order_status::index(status)) << 56 | uint64_t(order_side::index(side)) << 48 |
                          uint64_t(order_type::index(type)) << 40;
        uint64_t price_factor = (side == order_side::BUY) ? std::numeric_limits<uint64_t>::max() - price : price;
        auto ret = order_match_idx_key::make_from_word_sequence<uint64_t>(sym_pair_id, option, price_factor, order_id);
        // print("make order match idx=", ret, "\n");
        return ret;
    }

    struct DEX_TABLE order_t {
        uint64_t order_id; // auto-increment
        uint64_t external_id; // external id
        name owner;
        uint64_t sym_pair_id; // id of symbol_pair_table
        order_type_t order_type;
        order_side_t order_side;
        asset price;
        asset limit_quant;
        asset frozen_quant;
        int64_t taker_ratio;
        int64_t maker_ratio;
        time_point create_time;
        asset matched_assets;      //!< total matched asset quantity
        asset matched_coins;       //!< total matched coin quantity
        asset matched_fee;        //!< total matched fees
        order_status_t  status;
        uint64_t primary_key() const { return order_id; }
        order_match_idx_key get_order_match_idx() const {
            return make_order_match_idx(sym_pair_id, status, order_side, order_type, price.amount, order_id);
        }

        uint64_t get_external_idx() const {
            return external_id;
        }

        void print() const {
            auto create_time = this->create_time.elapsed.count(); // print the ms value
            PRINT_PROPERTIES(
                PP(order_id),
                PP(external_id),
                PP(owner),
                PP0(sym_pair_id),
                PP(order_type),
                PP(order_side),
                PP(price),
                PP(limit_quant),
                PP(frozen_quant),
                PP(taker_ratio),
                PP(maker_ratio),
                PP(create_time),
                PP(matched_assets),
                PP(matched_coins),
                PP(matched_fee),
                PP(status)
            );

        }
    };

    using order_match_idx = indexed_by<"ordermatch"_n, const_mem_fun<order_t, order_match_idx_key,
           &order_t::get_order_match_idx>>;
    using order_external_idx = indexed_by<"orderextidx"_n, const_mem_fun<order_t, uint64_t,
           &order_t::get_external_idx>>;

    typedef eosio::multi_index<"order"_n, order_t, order_match_idx, order_external_idx> order_table;

    inline static order_table make_order_table(const name &self) {
        return order_table(self, self.value/*scope*/);
    }


    struct DEX_TABLE deal_item_t {
        uint64_t id;
        uint64_t sym_pair_id;
        uint64_t buy_order_id;
        uint64_t sell_order_id;
        asset deal_assets;
        asset deal_coins;
        asset deal_price;
        order_side_t taker_side;
        asset buy_fee;
        asset sell_fee;
        string memo;
        time_point deal_time;

        uint64_t primary_key() const { return id; }

        uint64_t get_buy_id() const {
            return buy_order_id;
        }
        uint64_t get_sell_id() const {
            return sell_order_id;
        }

        void print() const {
            auto deal_time = this->deal_time.elapsed.count(); // print the ms value
            PRINT_PROPERTIES(
                PP0(id),
                PP(sym_pair_id),
                PP(buy_order_id),
                PP(sell_order_id),
                PP(deal_assets),
                PP(deal_coins),
                PP(deal_price),
                PP(taker_side),
                PP(buy_fee),
                PP(sell_fee),
                PP(memo),
                PP(deal_time)
            );

        }

    };

    using deal_buy_idx = indexed_by<"dealbuyidx"_n, const_mem_fun<deal_item_t, uint64_t,
           &deal_item_t::get_buy_id>>;
    using deal_sell_idx = indexed_by<"dealsellidx"_n, const_mem_fun<deal_item_t, uint64_t,
           &deal_item_t::get_sell_id>>;

    typedef eosio::multi_index<"deal"_n, deal_item_t, deal_buy_idx, deal_sell_idx> deal_table;

    inline static deal_table make_deal_table(const name &self) {
        return deal_table(self, self.value/*scope*/);
    }

}// namespace dex