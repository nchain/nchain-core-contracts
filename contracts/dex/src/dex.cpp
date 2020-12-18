#include <dex.hpp>
#include <utils.hpp>
#include "dex_const.hpp"

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

int64_t parse_price(string_view str) {
   safe<int64_t> ret;
   to_int(str, ret);
   // TODO: check range of price
   return ret.value;
}

void validate_fee_ratio(int64_t ratio, const string &title) {
    check(ratio >= 0 && ratio <= FEE_RATIO_MAX,
          "The " + title + " out of range [0, " + std::to_string(FEE_RATIO_MAX) + "]");
}

int64_t parse_ratio(string_view str) {
   safe<int64_t> ret;
   to_int(str, ret);
   validate_fee_ratio(ret.value, "ratio");
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

inline int64_t power(int64_t base, int64_t exp) {
    int64_t ret = 1;
    while( exp > 0  ) {
        ret *= base; --exp;
    }
    return ret;
}

inline int64_t power10(int64_t exp) {
    return power(10, exp);
}

inline int64_t calc_precision(int64_t digit) {
    check(digit <= 18, "precision digit " + std::to_string(digit) + " should be <= 18");
    return power10(digit);
}

int64_t calc_coin_amount(const asset &asset_quant, const int64_t price, const symbol &coin_symbol) {
    // should use the max precision to calc
    int64_t digit_diff = coin_symbol.precision() - asset_quant.symbol.precision();
    int64_t asset_amount = asset_quant.amount;
    if (digit_diff > 0) {
        asset_amount = multiply_decimal64(asset_amount, calc_precision(digit_diff), 1);
    }

    int64_t coin_amount = multiply_decimal64(asset_amount, price, PRICE_PRECISION);
    if (digit_diff < 0) {
        coin_amount = multiply_decimal64(coin_amount, 1, calc_precision(0 - digit_diff));
    }

    return coin_amount;
}

asset calc_coin_quant(const asset &asset_quant, const int64_t price, const symbol &coin_symbol) {
    return asset(calc_coin_amount(asset_quant, price, coin_symbol), coin_symbol);
}

inline string symbol_to_string(const symbol &s) {
    return std::to_string(s.precision()) + "," + s.code().to_string();
}

inline string symbol_pair_to_string(const symbol &asset_symbol, const symbol &coin_symbol) {
    return symbol_to_string(asset_symbol) + "/" + symbol_to_string(coin_symbol);
}

void dex::setconfig(const dex::config &conf) {
    require_auth( get_self() );
    check(is_account(conf.admin), "The admin account does not exist");
    check(is_account(conf.settler), "The settler account does not exist");
    check(is_account(conf.payee), "The payee account does not exist");
    check(is_account(conf.bank), "The bank account does not exist");
    validate_fee_ratio(conf.maker_ratio, "maker_ratio");
    validate_fee_ratio(conf.taker_ratio, "taker_ratio");

    _conf_tbl.set(conf, get_self());
}

void dex::setsympair(const symbol &asset_symbol, const symbol &coin_symbol,
                     const asset &min_asset_quant, const asset &min_coin_quant, bool enabled) {
    require_auth( _config.admin );
    auto sym_pair_tbl = make_symbol_pair_table(get_self());
    check(asset_symbol.is_valid(), "Invalid asset symbol");
    check(coin_symbol.is_valid(), "Invalid coin symbol");
    check(asset_symbol == min_asset_quant.symbol, "Incorrect symbol of min_coin_quant");
    check(coin_symbol == min_coin_quant.symbol, "Incorrect symbol of min_asset_quant");

     auto sym_pair_id = sym_pair_tbl.available_primary_key();

    // auto index = sym_pair_tbl.get_index<symbols_idx::index_name>();
    auto index = sym_pair_tbl.get_index<"symbolsidx"_n>();
    check( index.find( make_symbols_idx(asset_symbol, coin_symbol) ) == index.end(), "The symbol pair exist");
    check( index.find( revert_symbols_idx(asset_symbol, coin_symbol) ) == index.end(), "The reverted symbol pair exist");

    check( sym_pair_tbl.find(sym_pair_id) == sym_pair_tbl.end(), "The symbol pair id exist");

    sym_pair_tbl.emplace(get_self(), [&](auto &sym_pair) {
        sym_pair.sym_pair_id = sym_pair_tbl.available_primary_key();
        sym_pair.asset_symbol = asset_symbol;
        sym_pair.coin_symbol = coin_symbol;
        sym_pair.min_asset_quant = min_asset_quant;
        sym_pair.min_coin_quant = min_coin_quant;
        sym_pair.enabled = enabled;
    });
}

void dex::ontransfer(name from, name to, asset quantity, string memo) {
    if (from == get_self()) {
        return; // transfer out from this contract
    }
    check(to == get_self(), "Must transfer to this contract");
    check(quantity.amount > 0, "The quantity must be positive");
    auto bank = get_first_receiver();
    check(bank == _config.bank, "The bank must be " + _config.bank.to_string());

    vector<string_view> params = split(memo, ":");
    if (params.size() == 6 && params[0] == "order") {
      // order:<type>:<side>:<asset_quant>:<coin_quant>:<price>
        order_t order;
        order.order_type = parse_order_type(params[1]);
        order.order_side = parse_order_side(params[2]);
        order.asset_quant = asset_from_string(params[3]);
        order.coin_quant = asset_from_string(params[4]);
        order.price = parse_price(params[5]);

        auto sym_pair_tbl = make_symbol_pair_table(get_self());
        check(order.asset_quant.symbol.is_valid(), "Invalid asset symbol");
        check(order.coin_quant.symbol.is_valid(), "Invalid coin symbol");

        // auto index = sym_pair_tbl.get_index<symbols_idx::index_name>();
        auto index = sym_pair_tbl.get_index<"symbolsidx"_n>();
        auto sym_pair_it = index.find( make_symbols_idx(order.asset_quant.symbol, order.coin_quant.symbol) );
        check( sym_pair_it != index.end(),
            "The symbol pair '" + symbol_pair_to_string(order.asset_quant.symbol, order.coin_quant.symbol) + "' does not exist");

        // check amount

        if (order.order_side == order_side::BUY) {
            // the frozen token is coins, save in order.coin_quant
            check(order.coin_quant.amount == quantity.amount, "The input coin_quant must be equal to the transfer quantity for sell order");
            if (order.order_type == order_type::LIMIT_PRICE) {
                // check(false, "assets=" + std::to_string(order.asset_quant.amount) +
                //     ", price=" + std::to_string(order.price) +
                //     ", input=" + std::to_string(order.coin_quant.amount) +
                //     ", calc_coin_amount=" + std::to_string(calc_coin_amount(order.asset_quant, order.price, order.coin_quant.symbol)));
                // the deal limit amount is assets, save in order.asset_quant
                check(order.asset_quant >= sym_pair_it->min_asset_quant,
                      "The input asset_quant is too smaller than " +
                          sym_pair_it->min_asset_quant.to_string());
                check(order.coin_quant == calc_coin_quant(order.asset_quant, order.price, order.coin_quant.symbol),
                    "The input coin_quant must be equal to the calc_coin_quant for limit buy order");
            } else { //(order.order_type == order_type::MARKET_PRICE)
                // the deal limit amount is coins, save in order.coin_quant
                check(order.asset_quant.amount == 0, "The input asset amount must be 0 for market buy order");
                check(order.coin_quant >= sym_pair_it->min_coin_quant,
                      "The input coin_quant is smaller than " +
                          sym_pair_it->min_coin_quant.to_string());
            }
        } else { // (order.order_side == order_side::SELL)
            // the frozen token is assets, save in order.asset_quant
            // the deal limit amount is assets, save in order.asset_quant
            check(order.coin_quant.amount == 0, "The input coin amount must be 0 for buy order");
            check(order.asset_quant == quantity, "The input asset_quant must be equal to the transfer quantity for buy order");
            check(order.asset_quant >= sym_pair_it->min_asset_quant,
                  "The input asset_quant is too smaller than " +
                      sym_pair_it->min_asset_quant.to_string());
        }

        // TODO: need to add the total order coin/asset amount?

        order_table order_tbl(get_self(), get_self().value);
        order.order_id = order_tbl.available_primary_key();
        order.owner = from;

        check(order_tbl.find(order.order_id) == order_tbl.end(), "The order is exist. order_id=" + std::to_string(order.order_id));

        order_tbl.emplace( get_self(), [&]( auto& o ) {
            o = order;
        });
    } else {
        check(false, "Unsupport params of memo=" + memo);
    }
}

string get_taker_side(const dex::order_t &buy_order, const dex::order_t &sell_order) {
    string taker_side;
    if (buy_order.order_type != sell_order.order_type) {
        if (buy_order.order_type == order_type::MARKET_PRICE) {
            taker_side = order_side::BUY;
        } else {
            // assert(sell_order.order_type == order_type::MARKET_PRICE);
            taker_side = order_side::SELL;
        }
    } else { // buy_order.order_type == sell_order.order_type
        taker_side = (buy_order.order_id < sell_order.order_id) ? order_side::SELL : order_side::BUY;
    }
    return taker_side;
}

int64_t calc_match_fee(const dex::order_t &order, const dex::config &_config, const string &taker_side, int64_t amount) {

    int64_t ratio = 0;
    if (order.order_side == taker_side) {
        ratio = _config.taker_ratio;
    } else {
        ratio = _config.maker_ratio;
    }

    int64_t fee = multiply_decimal64(amount, ratio, RATIO_PRECISION);
    check(fee <= amount, "invalid match fee ratio=" + std::to_string(ratio));
    return fee;
}

int64_t sub_fee(int64_t &amount, int64_t fee, const string &msg) {
    check(amount > fee, "the fee exeed the amount of " + msg);
    return amount - fee;
}

void transfer_out(const name &contract, const name &bank, const name &to, const asset &quantity,
                  const string &memo) {

    action(permission_level{contract, "active"_n}, bank, "transfer"_n,
           std::make_tuple(contract, to, quantity, memo))
        .send();
}

void dex::settle(const uint64_t &buy_id, const uint64_t &sell_id, const asset &asset_quant,
                 const asset &coin_quant, const int64_t &price, const string &memo) {

    require_auth( _config.settler );

    //1. get and check buy_order and sell_order
    order_table order_tbl(get_self(), get_self().value);
    const auto &buy_order = order_tbl.get(buy_id, "unable to find buy order");
    const auto &sell_order = order_tbl.get(sell_id, "unable to find sell order");
    check(!buy_order.is_finish, "the buy order is finish");
    check(!sell_order.is_finish, "the sell order is finish");

    // 2. check and get order side
    // 2.1 check order side
    check(buy_order.order_side == order_side::BUY && sell_order.order_side == order_side::SELL,
          "order side mismatch");
    // 2.2 get taker side
    auto taker_side = get_taker_side(buy_order, sell_order);

    // 3. check coin pair type match
    check(buy_order.coin_quant.symbol == coin_quant.symbol &&
              buy_order.asset_quant.symbol == asset_quant.symbol,
          "buy order coin pair mismatch");
    check(sell_order.coin_quant.symbol == coin_quant.symbol &&
              sell_order.asset_quant.symbol == asset_quant.symbol,
          "sell order coin pair mismatch");

    // 4. check price match
    check(price > 0, "The deal price must be positive");
    if (buy_order.order_type == order_type::LIMIT_PRICE && sell_order.order_type == order_type::LIMIT_PRICE) {
        check(price <= buy_order.price, "the deal price must <= buy_order.price");
        check(price >= sell_order.price, "the deal price must >= sell_order.price");
    } else if (buy_order.order_type == order_type::LIMIT_PRICE && sell_order.order_type == order_type::MARKET_PRICE) {
        check(price == buy_order.price, "the deal price must == buy_order.price when sell_order is MARKET_PRICE");
    } else if (buy_order.order_type == order_type::MARKET_PRICE && sell_order.order_type == order_type::LIMIT_PRICE) {
        check(price == sell_order.price, "the deal price must == sell_order.price when buy_order is MARKET_PRICE");
    } else {
        check(buy_order.order_type == order_type::MARKET_PRICE && sell_order.order_type == order_type::MARKET_PRICE, "order type mismatch");
    }

    // 5. check deal amount match
    check(coin_quant.amount > 0 && asset_quant.amount > 0, "The deal amounts must be positive");
    int64_t deal_coin_diff = coin_quant.amount - calc_coin_amount(asset_quant, price, coin_quant.symbol);
    bool is_coin_amount_match = false;
    if (buy_order.order_type == order_type::MARKET_PRICE) {
        is_coin_amount_match = (std::abs(deal_coin_diff) <= std::max<int64_t>(1, (1 * price / PRICE_PRECISION)));
    } else {
        is_coin_amount_match = (deal_coin_diff == 0);
    }
    check(is_coin_amount_match, "The deal coin amount mismatch with the calc_coin_amount");

    uint64_t buyer_deal_coin_amount = buy_order.deal_coin_amount + coin_quant.amount;
    uint64_t buyer_deal_asset_amount = buy_order.deal_asset_amount + asset_quant.amount;

    uint64_t seller_deal_coin_amount = sell_order.deal_coin_amount + coin_quant.amount;
    uint64_t seller_deal_asset_amount = sell_order.deal_asset_amount + asset_quant.amount;

    // 6. check the order amount limits
    // 6.1 check the buy_order amount limit
    if (buy_order.order_type == order_type::MARKET_PRICE) {
        check(buy_order.coin_quant.amount >= buyer_deal_coin_amount,
            "the deal coin_quant.amount exceed residual coin amount of buy_order");
    } else {
        check(buy_order.asset_quant.amount >= buyer_deal_asset_amount,
            "the deal asset_quant.amount exceed residual asset amount of buy_order");
    }

    // 6.2 check the buy_order amount limit
    {
        check(sell_order.asset_quant.amount >= seller_deal_asset_amount,
            "the deal asset_quant.amount exceed residual asset amount of sell_order");
    }

    // the seller receive coins
    int64_t seller_recv_coins = coin_quant.amount;
    // the buyer receive assets
    int64_t buyer_recv_assets = asset_quant.amount;

    // 7. calc match fees
    // 7.1 calc match asset fee payed by buyer
    int64_t asset_match_fee = calc_match_fee(buy_order, _config, taker_side, buyer_recv_assets);
    if (asset_match_fee > 0) {
        buyer_recv_assets = sub_fee(buyer_recv_assets, asset_match_fee, "buyer_recv_assets");
        // pay the asset_match_fee to payee
        transfer_out(get_self(), _config.bank, _config.payee, asset(asset_match_fee, asset_quant.symbol), "asset_match_fee");
    }

    // 7.2. calc match coin fee payed by seller for exhange
    int64_t coin_match_fee = calc_match_fee(sell_order, _config, taker_side, seller_recv_coins);
    if (coin_match_fee) {
        seller_recv_coins = sub_fee(seller_recv_coins, coin_match_fee, "seller_recv_coins");
        // pay the coin_match_fee to payee
        transfer_out(get_self(), _config.bank, _config.payee, asset(coin_match_fee, coin_quant.symbol), "coin_match_fee");
    }

    // 8. transfer the coins and assets to seller and buyer
    // 8.1. transfer the coins to seller
    transfer_out(get_self(), _config.bank, sell_order.owner, asset(seller_recv_coins, coin_quant.symbol), "seller_recv_coins");
    // 8.2. transfer the assets to buyer
    transfer_out(get_self(), _config.bank, buy_order.owner, asset(buyer_recv_assets, asset_quant.symbol), "buyer_recv_assets");

    // 9. check order fullfiled to del or update
    // 9.1 check buy order fullfiled to del or update
    bool buy_order_finish = false;
    bool buy_order_fulfilled = false;
    if (buy_order.order_type == order_type::LIMIT_PRICE) {
        buy_order_finish = (buy_order.asset_quant.amount == buyer_deal_asset_amount);
        if (buy_order_finish) {
            if (buy_order.coin_quant.amount > buyer_deal_coin_amount) {
                int64_t refund_coins = buy_order.coin_quant.amount - buyer_deal_coin_amount;
                transfer_out(get_self(), _config.bank, buy_order.owner, asset(refund_coins, coin_quant.symbol), "refund_coins");
            }
        }
    } else { // buy_order.order_type == order_type::MARKET_PRICE
        buy_order_finish = buy_order.coin_quant.amount == buy_order.deal_coin_amount;
    }
    // udpate buy order
    order_tbl.modify(buy_order, get_self(), [&]( auto& a ) {
        a.deal_asset_amount = buyer_deal_asset_amount;
        a.deal_coin_amount = buyer_deal_coin_amount;
        a.is_finish = buy_order_finish;
    });

    // 9.2 check sell order fullfiled to del or update
    bool sell_order_finish = (sell_order.asset_quant.amount == seller_deal_asset_amount);
    // udpate sell order
    order_tbl.modify(sell_order, get_self(), [&]( auto& a ) {
        a.deal_asset_amount = seller_deal_asset_amount;
        a.deal_coin_amount = seller_deal_coin_amount;
        a.is_finish = sell_order_finish;
            a = sell_order;
    });
}

void dex::cancel(const uint64_t &order_id) {
    auto order_tbl = make_order_table(get_self());
    auto it = order_tbl.find(order_id);
    check(it != order_tbl.end(), "The order does not exist");
    auto order = *it;
    check(!order.is_finish, "The order is finish");
    // TODO: support the owner auth to cancel order?
    require_auth(order.owner);
    asset quantity;
    if (order.order_side == order_side::BUY) {
        check(order.coin_quant.amount > order.deal_coin_amount, "Invalid order coin amount");
        quantity = asset(order.coin_quant.amount - order.deal_coin_amount, order.coin_quant.symbol);
    } else {
        // order.order_side == order_side::SELL
        check(order.asset_quant.amount > order.deal_asset_amount, "Invalid order asset amount");
        quantity = asset(order.asset_quant.amount - order.deal_asset_amount, order.asset_quant.symbol);
    }
    transfer_out(get_self(), _config.bank, order.owner, quantity, "cancel_order");

    order_tbl.modify(it, order.owner, [&]( auto& a ) {
        a = order;
    });
}

dex::config dex::get_default_config() {
    check(is_account(BANK), "The default bank account does not exist");
    return {
        get_self(),             // name admin;
        get_self(),             // name settler;
        get_self(),             // name payee;
        BANK,                   // name bank;
        DEX_MAKER_FEE_RATIO,    // int64_t maker_ratio;
        DEX_TAKER_FEE_RATIO     // int64_t taker_ratio;
    };
}
