#pragma once

#include <eosio/eosio.hpp>
#include "dex_const.hpp"
#include "table.hpp"

using namespace std;
using namespace eosio;



class [[eosio::contract]] dex_contract : public contract {
public:
    using contract::contract;

public:
    dex_contract(name receiver, name code, datastream<const char *> ds)
        : contract(receiver, code, ds), _conf_tbl(get_self(), get_self().value), _global_tbl(get_self(), get_self().value) {
        _config = _conf_tbl.exists() ? _conf_tbl.get() : get_default_config();
        _global = _global_tbl.exists() ? _global_tbl.get() : dex::global{};
    }

    [[eosio::action]] void setconfig(const dex::config &conf);

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
    // TODO: const set<uint64_t> &sym_pairs
    [[eosio::action]] void match();

    [[eosio::action]] void cancel(const uint64_t &order_id);

    using setconfig_action = action_wrapper<"setconfig"_n, &dex_contract::setconfig>;
    using setsympair_action = action_wrapper<"setsympair"_n, &dex_contract::setsympair>;
    using ontransfer_action = action_wrapper<"ontransfer"_n, &dex_contract::ontransfer>;
    // using settle_action     = action_wrapper<"settle"_n, &dex::settle>;
    using match_action     = action_wrapper<"match"_n, &dex_contract::match>;
    using cancel_action     = action_wrapper<"cancel"_n, &dex_contract::cancel>;
private:
    dex::config get_default_config();
    // void process_order(order_t &order);
    void process_refund(dex::order_t &buy_order);

    dex::config_table _conf_tbl;
    dex::config _config;
    dex::global_table _global_tbl;
    dex::global _global;
};
