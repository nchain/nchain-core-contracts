#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>
#include "dex_const.hpp"

using namespace std;
using namespace eosio;

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

    inline static symbol_pair_table make_symbol_pair_table(name self) {
        return symbol_pair_table(self, self.value/*scope*/);
    }

    struct [[eosio::table]] order_t {
        uint64_t order_id; // auto-increment
        name owner;
        string order_type;
        string order_side;
        asset asset_quant;
        asset coin_quant;
        int64_t price;

        int64_t deal_asset_amount = 0;      //!< total deal asset amount
        int64_t deal_coin_amount  = 0;      //!< total deal coin amount
        // TODO: should del the order when finish??
        bool is_finish            = false;  //!< order is finish
        uint64_t primary_key() const { return order_id; }
    };

    typedef eosio::multi_index<"order"_n, order_t> order_table;

    inline static order_table make_order_table(name self) {
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
    [[eosio::action]] void settle(const uint64_t &buy_id, const uint64_t &sell_id,
                                  const asset &asset_quant, const asset &coin_quant,
                                  const int64_t &price, const string &memo);

    [[eosio::action]] void cancel(const uint64_t &order_id);

    using setconfig_action = action_wrapper<"setconfig"_n, &dex::setconfig>;
    using setsympair_action = action_wrapper<"setsympair"_n, &dex::setsympair>;
    using ontransfer_action = action_wrapper<"ontransfer"_n, &dex::ontransfer>;
    using settle_action     = action_wrapper<"settle"_n, &dex::settle>;
    using cancel_action     = action_wrapper<"cancel"_n, &dex::cancel>;
private:
    config get_default_config();
    config_table _conf_tbl;
    config _config;
};
