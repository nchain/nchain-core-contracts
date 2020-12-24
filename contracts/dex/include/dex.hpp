#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>
#include "dex_const.hpp"

using namespace std;
using namespace eosio;

typedef name order_type_t;
typedef name order_side_t;

namespace order_type {
   static const order_type_t NONE = order_type_t();
   static const order_type_t LIMIT = "limit"_n;
   static const order_type_t MARKET = "market"_n;
   // order_type_t -> index
   static const map<order_type_t, uint8_t> ENUM_MAP = {
      {LIMIT, 1},
      {MARKET, 2}
   };
   inline bool is_valid(const order_type_t &value) {
      return ENUM_MAP.count(value);
   }
   inline uint8_t index(const order_type_t &value) {
        if (value == NONE) return 0;
        auto it = ENUM_MAP.find(value);
        check(it != ENUM_MAP.end(), "Invalid order_type=" + value.to_string());
        return it->second;
   }
}

namespace order_side {
   static const order_type_t NONE = order_type_t();
   static const order_side_t BUY = "buy"_n;
   static const order_side_t SELL = "sell"_n;
   // name -> index
   static const map<order_side_t, uint8_t> ENUM_MAP = {
      {BUY, 1},
      {SELL, 2}
   };
   inline bool is_valid(const order_side_t &value) {
      return ENUM_MAP.count(value);
   }
   inline uint8_t index(const order_side_t &value) {
        if (value == NONE) return 0;
        auto it = ENUM_MAP.find(value);
        check(it != ENUM_MAP.end(), "Invalid order_type=" + value.to_string());
        return it->second;
   }
}

class [[eosio::contract]] dex : public contract {
public:
    using contract::contract;

    struct [[eosio::table]] config {
        name admin;   // admin of this contract
        name settler; // settler
        name payee;   // payee of this contract
        name bank;    // bank
        int64_t maker_ratio;
        int64_t taker_ratio;
    };

    typedef eosio::singleton< "config"_n, config > config_table;

    static inline uint128_t make_symbols_idx(const symbol &asset_symbol, const symbol &coin_symbol) {
        return uint128_t(asset_symbol.raw()) << 64 | uint128_t(coin_symbol.raw());
    }

    static inline uint128_t revert_symbols_idx(const symbol &asset_symbol, const symbol &coin_symbol) {
        return uint128_t(coin_symbol.raw()) << 64 | uint128_t(asset_symbol.raw());
    }

    struct [[eosio::table]] symbol_pair_t {
        uint64_t sym_pair_id; // auto-increment
        symbol asset_symbol;
        symbol coin_symbol;
        asset min_asset_quant;
        asset min_coin_quant;
        bool enabled;

        uint128_t primary_key() const { return sym_pair_id; }
        uint128_t make_symbols_idx() const { return dex::make_symbols_idx(asset_symbol, coin_symbol); }
    };

    using symbols_idx = indexed_by<"symbolsidx"_n, const_mem_fun<symbol_pair_t, uint128_t,
           &symbol_pair_t::make_symbols_idx>>;

    typedef eosio::multi_index<"sympair"_n, symbol_pair_t, symbols_idx> symbol_pair_table;

    inline static symbol_pair_table make_symbol_pair_table(const name &self) {
        return symbol_pair_table(self, self.value/*scope*/);
    }

    using order_match_idx_key = fixed_bytes<32>;
    inline static order_match_idx_key make_order_match_idx(uint64_t sym_pair_id, const order_side_t &side, const order_type_t &type, uint64_t price, uint64_t order_id) {
        uint64_t option = uint64_t(order_side::index(side)) << 56 | uint64_t(order_type::index(type)) << 48;
        uint64_t price_factor = (side == order_side::BUY) ? std::numeric_limits<uint64_t>::max() - price : price;
        return order_match_idx_key::make_from_word_sequence<uint64_t>(sym_pair_id, option, price_factor, order_id);
    }

    struct [[eosio::table]] order_t {
        uint64_t sym_pair_id; // id of symbol_pair_table
        uint64_t order_id; // auto-increment
        name owner;
        order_type_t order_type;
        order_side_t order_side;
        asset asset_quant;
        asset coin_quant;
        int64_t price;

        int64_t deal_asset_amount = 0;      //!< total deal asset amount
        int64_t deal_coin_amount  = 0;      //!< total deal coin amount
        // TODO: should del the order when finish??
        bool is_complete            = false;  //!< order is finish
        uint64_t primary_key() const { return order_id; }
        order_match_idx_key get_order_match_idx() const {
            return dex::make_order_match_idx(sym_pair_id, order_side, order_type, price, order_id);
        }
    };

    using order_match_idx = indexed_by<"ordermatch"_n, const_mem_fun<order_t, order_match_idx_key,
           &order_t::get_order_match_idx>>;

    typedef eosio::multi_index<"order"_n, order_t, order_match_idx> order_table;

    inline static order_table make_order_table(const name &self) {
        return order_table(self, self.value/*scope*/);
    }

public:
    dex(name receiver, name code, datastream<const char *> ds) : contract(receiver, code, ds), _conf_tbl(get_self(), get_self().value) {
        _config = _conf_tbl.exists() ? _conf_tbl.get() : get_default_config();
    }

    [[eosio::action]] void setconfig(const config &conf);

    [[eosio::action]] void setsympair(const symbol &asset_symbol, const symbol &coin_symbol,
                                      const asset &min_asset_quant, const asset &min_coin_quant,
                                      bool enabled);

    [[eosio::on_notify("*::transfer")]] void ontransfer(name from, name to, asset quantity,
                                                        string memo);
    // todo: should use checksum to indicate the order
/*
    [[eosio::action]] void settle(const uint64_t &buy_id, const uint64_t &sell_id,
                                  const asset &asset_quant, const asset &coin_quant,
                                  const int64_t &price, const string &memo);
*/
    [[eosio::action]] void match();

    [[eosio::action]] void cancel(const uint64_t &order_id);

    using setconfig_action = action_wrapper<"setconfig"_n, &dex::setconfig>;
    using setsympair_action = action_wrapper<"setsympair"_n, &dex::setsympair>;
    using ontransfer_action = action_wrapper<"ontransfer"_n, &dex::ontransfer>;
    // using settle_action     = action_wrapper<"settle"_n, &dex::settle>;
    using match_action     = action_wrapper<"match"_n, &dex::match>;
    using cancel_action     = action_wrapper<"cancel"_n, &dex::cancel>;
private:
    config get_default_config();
    // void process_order(order_t &order);
    void process_refund(order_t &order);

    config_table _conf_tbl;
    config _config;
};
