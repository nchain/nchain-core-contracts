#pragma once

#include <eosio/eosio.hpp>

#include "dex_const.hpp"
#include "dex_states.hpp"
#include "dex_match.hpp"

using namespace std;
using namespace eosio;

class DEX_CONTRACT dex_contract : public contract {
public:
    using contract::contract;

public:
    dex_contract(name receiver, name code, datastream<const char *> ds)
        : contract(receiver, code, ds), _conf_tbl(get_self(), get_self().value),
          _global(dex::global_state::make_global(get_self())) {
        _config = _conf_tbl.exists() ? _conf_tbl.get() : get_default_config();
    }

    ~dex_contract() {
        _global->save(get_self());
    }

    [[eosio::action]] void init();
    
    [[eosio::action]] void setconfig(const dex::config &conf);

    [[eosio::action]] void setsympair(const extended_symbol &asset_symbol,
                                      const extended_symbol &coin_symbol,
                                      const asset &min_asset_quant, const asset &min_coin_quant,
                                      bool only_accept_coin_fee, bool enabled);

    [[eosio::action]] void onoffsympair(const uint64_t& sympair_id, const bool& on_off);

    [[eosio::on_notify("*::transfer")]] void ontransfer(const name& from, const name& to, const asset& quant, const string& memo);

    [[eosio::action]] void withdraw(const name& user, const name& to, const name &token_code, const asset& quant, const string& memo);

    /**
     * new order, should deposit by transfer first
     * @param user - user, owner of order
     * @param sympair_id - symbol pair id
     * @param order_type - order type, LIMIT | MARKET
     * @param order_side - order side, BUY | SELL
     * @param limit_quant - the limit quantity
     * @param frozen_quant - the frozen quantity, unused
     * @param price - the price, should be 0 for MARKET order
     * @param external_id - external id, always set by application
     * @param order_config_ex - optional extended config, must authenticate by admin if set
     */
    [[eosio::action]] void
    neworder(const name &user, const uint64_t &sympair_id,
             const name &order_type, const name &order_side,
             const asset &limit_quant, const asset &frozen_quant,
             const asset &price, const uint64_t &external_id,
             const optional<dex::order_config_ex_t> &order_config_ex);

    [[eosio::action]] void buymarket(const name &user, const uint64_t &sympair_id,
                                     const asset &coins, const uint64_t &external_id,
                                     const optional<dex::order_config_ex_t> &order_config_ex);

    [[eosio::action]] void sellmarket(const name &user, const uint64_t &sympair_id,
                                      const asset &quantity, const uint64_t &external_id,
                                      const optional<dex::order_config_ex_t> &order_config_ex);

    [[eosio::action]] void buylimit(const name &user, const uint64_t &sympair_id,
                                    const asset &quantity, const asset &price, const uint64_t &external_id,
                                    const optional<dex::order_config_ex_t> &order_config_ex);

    [[eosio::action]] void selllimit(const name &user, const uint64_t &sympair_id,
                                     const asset &quantity, const asset &price,
                                     const uint64_t &external_id,
                                     const optional<dex::order_config_ex_t> &order_config_ex);

    /**
     *  @param max_count the max count of match item
     *  @param sym_pairs the symol pairs to match. is empty, match all
     */
    [[eosio::action]] void match(const name &matcher, uint32_t max_count, const vector<uint64_t> &sym_pairs, const string &memo);

    [[eosio::action]] void cancel(const uint64_t &order_id);

    [[eosio::action]] void cleandata(const uint64_t &max_count);

    [[eosio::action]] void version();

    [[eosio::action]] void name2uint(const name& n) { check(false, to_string(n.value)); };

    using setconfig_action  = action_wrapper<"setconfig"_n, &dex_contract::setconfig>;
    using setsympair_action = action_wrapper<"setsympair"_n, &dex_contract::setsympair>;
    using ontransfer_action = action_wrapper<"ontransfer"_n, &dex_contract::ontransfer>;
    using withdraw_action   = action_wrapper<"withdraw"_n, &dex_contract::withdraw>;
    using neworder_action   = action_wrapper<"neworder"_n, &dex_contract::neworder>;
    using buymarket_action  = action_wrapper<"buymarket"_n, &dex_contract::buymarket>;
    using sellmarket_action = action_wrapper<"sellmarket"_n, &dex_contract::sellmarket>;
    using buylimit_action   = action_wrapper<"buylimit"_n, &dex_contract::buylimit>;
    using selllimit_action  = action_wrapper<"selllimit"_n, &dex_contract::selllimit>;
    using match_action      = action_wrapper<"match"_n, &dex_contract::match>;
    using cancel_action     = action_wrapper<"cancel"_n, &dex_contract::cancel>;
    using version_action    = action_wrapper<"version"_n, &dex_contract::version>;
    using name2uint_action = action_wrapper<"name2uint"_n, &dex_contract::name2uint>;

private:
    dex::config get_default_config();
    void process_refund(dex::order_t &buy_order);
    void match_sym_pair(const name &matcher, const dex::symbol_pair_t &sym_pair, uint32_t max_count,
                        uint32_t &matched_count, const string &memo);

    void new_order(const name &user, const uint64_t &sympair_id,
            const name &order_type, const name &order_side,
            const asset &limit_quant,
            const optional<asset> &price,
            const uint64_t &external_id,
            const optional<dex::order_config_ex_t> &order_config_ex);

    void add_balance(const name &user, const name &bank, const asset &quantity, const name &ram_payer);

    inline void sub_balance(const name &user, const name &bank, const asset &quantity, const name &ram_payer) {
        ASSERT(quantity.amount >= 0);
        add_balance(user, bank, -quantity, ram_payer);
    }

    bool check_data_outdated(const time_point &data_time, const time_point &now);

    bool check_dex_enabled();

    dex::config_table _conf_tbl;
    dex::config _config;
    dex::global_state::ptr_t _global;
};
