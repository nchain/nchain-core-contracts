#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>

using namespace std;
using namespace eosio;

class [[eosio::contract]] dex : public contract {
public:
    using contract::contract;

   inline static const name CONFIG_KEY = "config"_n;

   struct [[eosio::table]] config_t {
       name owner;   // owner of this contract
       name settler; // settler
       name payee;   // payee of this contract
       uint64_t primary_key() const { return CONFIG_KEY.value; }
   };

    typedef eosio::multi_index<"config"_n, config_t> config_table;

    inline static config_table make_config_table(name self) {
        return config_table(self, self.value/*scope*/);
    }

    struct [[eosio::table]] exchange_t {
        name ex_id;
        name owner;
        name payee;
        string open_mode;
        int64_t maker_ratio;
        int64_t taker_ratio;
        string url;
        string memo;
        uint64_t primary_key() const { return ex_id.value; }
    };

    typedef eosio::multi_index<"exchange"_n, exchange_t> exchange_table;

    inline static exchange_table make_exchange_table(name self) {
        return exchange_table(self, self.value/*scope*/);
    }

    struct [[eosio::table]] order_t {
        uint64_t order_id; // auto-increment
        name owner;
        string order_type;
        string order_side;
        asset coin_quant;
        asset asset_quant;
        int64_t price;

        int64_t deal_coin_amount  = 0;      //!< total deal coin amount
        int64_t deal_asset_amount = 0;      //!< total deal asset amount
        bool is_finish            = false;  //!< order is finish
        uint64_t primary_key() const { return order_id; }
    };

    typedef eosio::multi_index<"order"_n, order_t> order_table;

    inline static order_table make_order_table(name self) {
        return order_table(self, self.value/*scope*/);
    }

public:
    dex(name receiver, name code, datastream<const char *> ds) : contract(receiver, code, ds) {}

   [[eosio::action]] void init(const name &owner, const name &settler, const name &payee);

    [[eosio::on_notify("*::transfer")]] void ontransfer(name from, name to, asset quantity,
                                                        string memo);

    [[eosio::action]] void settle(const uint64_t &buy_id, const uint64_t &sell_id,
                                  const int64_t &price, const asset &coin_quant,
                                  const asset &asset_quant, const string &memo);

    [[eosio::action]] void cancel(const uint64_t &order_id);

    using init_action = action_wrapper<"init"_n, &dex::init>;
    using ontransfer_action = action_wrapper<"ontransfer"_n, &dex::ontransfer>;
    using settle_action     = action_wrapper<"settle"_n, &dex::settle>;
    using cancel_action     = action_wrapper<"cancel"_n, &dex::cancel>;
private:
    config_t get_config();
};
