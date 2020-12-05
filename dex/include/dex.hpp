#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>

using namespace std;
using namespace eosio;

class [[eosio::contract]] dex : public contract {
   public:
      using contract::contract;
      dex( name receiver, name code, datastream<const char*> ds )
         : contract(receiver, code, ds) {}

      struct [[eosio::table]] exchange_t {
         name ex_id;
         name owner;
         name payee;
         string open_mode;
         uint64_t maker_ratio;
         uint64_t taker_ratio;
         string url;
         string memo;
         uint64_t primary_key()const { return ex_id.value; }

      };

      typedef eosio::multi_index<"exchange"_n, exchange_t> exchange_table;

      struct [[eosio::table]] order_t {
         uint64_t order_id; // auto-increment
         name owner;
         string order_type;
         string order_side;
         asset coin_quant;
         asset asset_quant;
         uint64_t price;
         uint64_t primary_key()const { return order_id; }

      };

      typedef eosio::multi_index<"order"_n, order_t> order_table;

      [[eosio::on_notify("*::transfer")]] void ontransfer(name from, name to, asset quantity, string memo);

      using ontransfer_action = action_wrapper<"ontransfer"_n, &dex::ontransfer>;
};
