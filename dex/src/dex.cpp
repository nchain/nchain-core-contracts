#include <dex.hpp>
#include <utils.hpp>

using namespace eosio;
using namespace std;

namespace order_type {
   static const string_view LIMIT_PRICE = "limit_price";
   static const string_view MARKET_PRICE = "market_price";
   static const set<string_view> MODES = {
      LIMIT_PRICE, MARKET_PRICE
   };
   inline bool is_valid(const string_view &mode) {
      return MODES.count(mode);
   }
}

namespace order_side {
   static const string_view BUY = "buy";
   static const string_view SELL = "sell";
   static const set<string_view> MODES = {
      BUY, SELL
   };
   inline bool is_valid(const string_view &mode) {
      return MODES.count(mode);
   }
}

namespace open_mode {
   static const string_view PUBLIC = "public";
   static const string_view PRIVATE = "private";
   static const set<string_view> MODES = {
      PUBLIC, PRIVATE
   };
   inline bool is_valid(const string_view &mode) {
      return MODES.count(mode);
   }
}

uint64_t parse_price(string_view str) {
   safe<int64_t> ret;
   to_int(str, ret);
   // TODO: check range of price
   return ret.value;
}

uint64_t parse_ratio(string_view str) {
   safe<int64_t> ret;
   to_int(str, ret);
   // TODO: check range of ratio
   return ret.value;
}

string parse_order_type(string_view str) {
    check(order_type::is_valid(str), "invalid order_type=" + string{str});
    return string{str};
}

string parse_order_side(string_view str) {
    check(order_side::is_valid(str), "invalid order_side=" + string{str});
    return string{str};
}

string parse_open_mode(string_view str) {
    check(open_mode::is_valid(str), "invalid open_mode=" + string{str});
    return string{str};
}

void dex::ontransfer(name from, name to, asset quantity, string memo) {
    constexpr string_view DEPOSIT_TO = "deposit to:";
    constexpr string_view EXCHANGE   = "exchange:";

    if (from == get_self())
        return; // transfer out from this contract
    check(to == get_self(), "Must transfer to this contract");
    check(quantity.amount >= 0, "quantity must be positive");
    auto bank = get_first_receiver();
    // TODO: check bank
    constexpr name BANK = "nchain.token"_n;
    check(bank == BANK, "the bank must be " + BANK.to_string());

    vector<string_view> params = split(memo, ":");
    if (params.size() == 7 && params[0] == "order") {
      // order:<type>:<side>:<coin_quant>:<asset_quant>:<price>:<ex_id>
        order_t order;
        order.order_type = parse_order_type(params[1]);
        order.order_side = parse_order_side(params[2]);
        order.coin_quant = asset_from_string(params[3]);
        order.asset_quant = asset_from_string(params[4]);
        order.price = parse_price(params[5]);

        // TODO: check coin_pair exist

        // check amount
        if (order.order_type == order_type::MARKET_PRICE && order.order_side == order_side::BUY) {
            check(order.coin_quant.amount == quantity.amount, "coin amount must be equal to the transfer quantity");
            // TODO: check coin amount range
            check(order.asset_quant.amount == 0, "asset amount must be 0 for market buy order");
        } else {
            check(order.coin_quant.amount == 0, "coin amount must be 0");
            check(order.asset_quant.amount == quantity.amount, "asset amount must be equal to the transfer quantity");
            // TODO: check asset amount range
        }

        // TODO: need to add the total order coin/asset amount?

        order_table order_tbl(get_self(), get_self().value);
        order.order_id = order_tbl.available_primary_key();
        order.owner = from;

        check(order_tbl.find(order.order_id) == order_tbl.end(), "The order is exist. order_id=" + std::to_string(order.order_id));

        order_tbl.emplace( from, [&]( auto& o ) {
            o = order;
        });
    }
    if (params.size() == 8 && params[0] == "ex") {
        // ex:<ex_id>:<owner>:<payee>:<open_mode>:<maker_ratio>:<taker_ratio>:<url>:<memo>
        exchange_t exchange;
        exchange.ex_id       = name(params[1]);
        exchange.owner       = name(params[2]);
        exchange.payee       = name(params[3]);
        exchange.open_mode   = parse_open_mode(params[4]);
        exchange.maker_ratio = parse_ratio(params[5]);
        exchange.taker_ratio = parse_ratio(params[6]);
        exchange.url         = string{params[7]};
        exchange.memo        = string{params[8]};

        exchange_table ex_tbl(get_self(), get_self().value);
        const auto &it = ex_tbl.find(exchange.primary_key());
        check(it == ex_tbl.end(), "The exchange is exist. ex_id=" + exchange.ex_id.to_string());
        // TODO: process reg exchange fee
        ex_tbl.emplace( from, [&]( auto& ex ) {
            ex = exchange;
        });
    }
}