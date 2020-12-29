#include <dex.hpp>
#include <utils.hpp>
#include "dex_const.hpp"

using namespace eosio;
using namespace std;
using namespace dex;

int64_t parse_price(string_view str) {
   safe<int64_t> ret;
   to_int(str, ret);
   // TODO: check range of price
   return ret.value;
}

uint64_t parse_external_id(string_view str) {
   safe<uint64_t> ret;
   to_int(str, ret);
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

name parse_order_type(string_view str) {
    name ret(str);
    check(order_type::is_valid(ret), "invalid order_type=" + string{str});
    return ret;
}

name parse_order_side(string_view str) {
    name ret(str);
    check(order_side::is_valid(ret), "invalid order_side=" + string{str});
    return ret;
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

int64_t calc_asset_amount(const asset &coin_quant, const int64_t price, const symbol &asset_symbol) {
    // should use the max precision to calc
    int64_t digit_diff = asset_symbol.precision() - coin_quant.symbol.precision();
    int64_t coin_amount = coin_quant.amount;
    if (digit_diff > 0) {
        coin_amount = multiply_decimal64(coin_amount, calc_precision(digit_diff), 1);
    }

    int64_t asset_amount = divide_decimal64(coin_amount, price, PRICE_PRECISION);
    if (digit_diff < 0) {
        asset_amount = multiply_decimal64(asset_amount, 1, calc_precision(0 - digit_diff));
    }

    return asset_amount;
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

void dex_contract::setconfig(const dex::config &conf) {
    require_auth( get_self() );
    check(is_account(conf.admin), "The admin account does not exist");
    check(is_account(conf.settler), "The settler account does not exist");
    check(is_account(conf.payee), "The payee account does not exist");
    check(is_account(conf.bank), "The bank account does not exist");
    validate_fee_ratio(conf.maker_ratio, "maker_ratio");
    validate_fee_ratio(conf.taker_ratio, "taker_ratio");

    _conf_tbl.set(conf, get_self());
}

void dex_contract::setsympair(const symbol &asset_symbol, const symbol &coin_symbol,
                     const asset &min_asset_quant, const asset &min_coin_quant, bool enabled) {
    require_auth( _config.admin );
    auto sym_pair_tbl = make_symbol_pair_table(get_self());
    check(asset_symbol.is_valid(), "Invalid asset symbol");
    check(coin_symbol.is_valid(), "Invalid coin symbol");
    check(asset_symbol.code() != coin_symbol.code(), "Error: asset_symbol.code() == coin_symbol.code()");
    check(asset_symbol == min_asset_quant.symbol, "Incorrect symbol of min_coin_quant");
    check(coin_symbol == min_coin_quant.symbol, "Incorrect symbol of min_asset_quant");


    auto index = sym_pair_tbl.get_index<static_cast<name::raw>(symbols_idx::index_name)>();
    check( index.find( revert_symbols_idx(asset_symbol, coin_symbol) ) == index.end(), "The reverted symbol pair exist");

    auto it = index.find( make_symbols_idx(asset_symbol, coin_symbol));
    if (it == index.end()) {
        // new sym pair
        auto sym_pair_id = dex::new_sym_pair_id(*_global);
        check( sym_pair_tbl.find(sym_pair_id) == sym_pair_tbl.end(), "The symbol pair id exist");
        sym_pair_tbl.emplace(get_self(), [&](auto &sym_pair) {
            sym_pair.sym_pair_id = sym_pair_id;
            sym_pair.asset_symbol = asset_symbol;
            sym_pair.coin_symbol = coin_symbol;
            sym_pair.min_asset_quant = min_asset_quant;
            sym_pair.min_coin_quant = min_coin_quant;
            sym_pair.enabled = enabled;
        });
    } else {
        sym_pair_tbl.modify(*it, same_payer, [&]( auto& sym_pair ) {
            sym_pair.asset_symbol = asset_symbol;
            sym_pair.coin_symbol = coin_symbol;
            sym_pair.min_asset_quant = min_asset_quant;
            sym_pair.min_coin_quant = min_coin_quant;
            sym_pair.enabled = enabled;
        });
    }
}

void dex_contract::ontransfer(name from, name to, asset quantity, string memo) {
    if (from == get_self()) {
        return; // transfer out from this contract
    }
    check(to == get_self(), "Must transfer to this contract");
    check(quantity.amount > 0, "The quantity must be positive");
    auto bank = get_first_receiver();
    check(bank == _config.bank, "The bank must be " + _config.bank.to_string());

    vector<string_view> params = split(memo, ":");
    if (params.size() == 7 && params[0] == "order") {
      // order:<type>:<side>:<asset_quant>:<coin_quant>:<price>:ext_id
        order_t order;
        order.order_type = parse_order_type(params[1]);
        order.order_side = parse_order_side(params[2]);
        order.asset_quant = asset_from_string(params[3]);
        order.coin_quant = asset_from_string(params[4]);
        order.price = parse_price(params[5]);
        order.external_id = parse_external_id(params[6]);

        auto sym_pair_tbl = make_symbol_pair_table(get_self());
        check(order.asset_quant.symbol.is_valid(), "Invalid asset symbol");
        check(order.coin_quant.symbol.is_valid(), "Invalid coin symbol");

        // auto index = sym_pair_tbl.get_index<symbols_idx::index_name>();
        auto index = sym_pair_tbl.get_index<"symbolsidx"_n>();
        auto sym_pair_it = index.find( make_symbols_idx(order.asset_quant.symbol, order.coin_quant.symbol) );
        check( sym_pair_it != index.end(),
            "The symbol pair '" + symbol_pair_to_string(order.asset_quant.symbol, order.coin_quant.symbol) + "' does not exist");
        order.sym_pair_id = sym_pair_it->sym_pair_id;
        // check amount

        if (order.order_side == order_side::BUY) {
            // the frozen token is coins, save in order.coin_quant
            check(order.coin_quant.amount == quantity.amount, "The input coin_quant must be equal to the transfer quantity for sell order");
            if (order.order_type == order_type::LIMIT) {
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
            } else { //(order.order_type == order_type::MARKET)
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

        auto order_tbl = make_order_table(get_self());
        // TODO: implement auto inc id by global table
        order.order_id = dex::new_order_id(*_global);
        order.owner = from;

        check(order_tbl.find(order.order_id) == order_tbl.end(), "The order is exist. order_id=" + std::to_string(order.order_id));

        order_tbl.emplace( get_self(), [&]( auto& o ) {
            o = order;
        });
    } else {
        check(false, "Unsupport params of memo=" + memo);
    }
}

order_type_t get_taker_side(const dex::order_t &buy_order, const dex::order_t &sell_order) {
    order_type_t taker_side;
    if (buy_order.order_type != sell_order.order_type) {
        if (buy_order.order_type == order_type::MARKET) {
            taker_side = order_side::BUY;
        } else {
            // assert(sell_order.order_type == order_type::MARKET);
            taker_side = order_side::SELL;
        }
    } else { // buy_order.order_type == sell_order.order_type
        taker_side = (buy_order.order_id < sell_order.order_id) ? order_side::SELL : order_side::BUY;
    }
    return taker_side;
}

int64_t calc_match_fee(const dex::order_t &order, const dex::config &_config, const order_type_t &taker_side, int64_t amount) {

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
    print("transfer_out (", PP0(contract), PP(bank), PP(to), PP(quantity), PP(memo), ")\n");
    action(permission_level{contract, "active"_n}, bank, "transfer"_n,
           std::make_tuple(contract, to, quantity, memo))
        .send();
}

void dex_contract::cancel(const uint64_t &order_id) {
    auto order_tbl = make_order_table(get_self());
    auto it = order_tbl.find(order_id);
    check(it != order_tbl.end(), "The order does not exist or has been matched");
    auto order = *it;
    // TODO: support the owner auth to cancel order?
    require_auth(order.owner);
    asset quantity;
    if (order.order_side == order_side::BUY) {
        check(order.coin_quant.amount > order.matched_coins, "Invalid order coin amount");
        quantity = asset(order.coin_quant.amount - order.matched_coins, order.coin_quant.symbol);
    } else {
        // order.order_side == order_side::SELL
        check(order.asset_quant.amount > order.matched_assets, "Invalid order asset amount");
        quantity = asset(order.asset_quant.amount - order.matched_assets, order.asset_quant.symbol);
    }
    transfer_out(get_self(), _config.bank, order.owner, quantity, "cancel_order");

    order_tbl.modify(it, order.owner, [&]( auto& a ) {
        a = order;
    });
}

dex::config dex_contract::get_default_config() {
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

void get_match_amounts(const dex::order_t &taker_order, const dex::order_t &maker_order, uint64_t &match_assets, uint64_t &match_coins) {
    uint64_t match_price = maker_order.price;
    int64_t taker_free_assets = 0;
    if (taker_order.order_side == order_side::BUY && taker_order.order_type == order_type::MARKET) {
        taker_free_assets = calc_asset_amount(taker_order.coin_quant, match_price, taker_order.asset_quant.symbol);
        if (taker_free_assets == 0) { // dust amount
            check(maker_order.asset_quant.amount > 0, "The maker order asset_amount is 0");
            match_assets = 1;
            match_coins = taker_order.coin_quant.amount;
            return;
        }
    } else {
        check(taker_order.asset_quant.amount >= taker_order.matched_assets, "taker_order.asset_quant.amount >= taker_order.matched_assets");
        taker_free_assets = taker_order.asset_quant.amount - taker_order.matched_assets;
    }

    check(maker_order.asset_quant.amount >= maker_order.matched_assets, "maker_order.asset_quant.amount >= maker_order.matched_assets");
    int64_t maker_free_assets = maker_order.asset_quant.amount - maker_order.matched_assets;

    match_assets = std::min(taker_free_assets, maker_free_assets);
    match_coins = calc_coin_amount(asset(match_assets, taker_order.asset_quant.symbol), match_price, taker_order.coin_quant.symbol);
}

void dex_contract::process_refund(dex::order_t &buy_order) {
    ASSERT(buy_order.order_side == order_side::BUY);
    if (buy_order.order_type == order_type::LIMIT) {
        check(buy_order.matched_coins <= buy_order.coin_quant.amount,
              "The match coins is overflow for buy limit order " +
                  std::to_string(buy_order.order_id));
        if (buy_order.coin_quant.amount > buy_order.matched_coins) {
            int64_t refunds = buy_order.coin_quant.amount - buy_order.matched_coins;
            transfer_out(get_self(), _config.bank, buy_order.owner, asset(refunds, buy_order.coin_quant.symbol), "refund_coins");
            buy_order.matched_coins = buy_order.coin_quant.amount;
        }
    }
}

void dex_contract::match() {
    require_auth( _config.settler );
    // TODO: validate sym_pairs??
    // get sym_pair_list
    std::list<symbol_pair_t> sym_pair_list;
    auto sym_pair_tbl = dex::make_symbol_pair_table(get_self());
    auto sym_pair_it = sym_pair_tbl.begin();
    for (; sym_pair_it != sym_pair_tbl.end(); sym_pair_it++) {
        // TODO: check is enabled
        sym_pair_list.push_back(*sym_pair_it);
    }

    int32_t matched_count = 0;
    for (const auto &sym_pair : sym_pair_list) {
        if (matched_count < DEX_MATCH_COUNT_MAX) {
            match_sym_pair(sym_pair, matched_count);
        }
    }
    check(matched_count > 0, "The matched count == 0");
}


template<typename match_index_t>
class matching_pair_iterator {
public:
    using order_iterator = matching_order_iterator<match_index_t>;
    using order_iterator_vector = std::vector<order_iterator*>;

    matching_pair_iterator(match_index_t &match_index, const dex::symbol_pair_t &sym_pair)
        : _match_index(match_index), _sym_pair(sym_pair),
          limit_buy_it(match_index, sym_pair.sym_pair_id, order_side::BUY, order_type::LIMIT),
          limit_sell_it(match_index, sym_pair.sym_pair_id, order_side::SELL, order_type::LIMIT),
          market_buy_it(match_index, sym_pair.sym_pair_id, order_side::BUY, order_type::MARKET),
          market_sell_it(match_index, sym_pair.sym_pair_id, order_side::SELL, order_type::MARKET) {

        process_data();
    }

    template<typename table_t>
    void complete_and_next(table_t &table) {
        if (_taker_it->is_complete()) {
            _taker_it->complete_and_next(table);
        }
        if (_maker_it->is_complete()) {
            _maker_it->complete_and_next(table);
        }
        process_data();
    }

    template<typename table_t>
    void save_matching_order(table_t &table) {
        limit_buy_it.save_matching_order(table);
        limit_sell_it.save_matching_order(table);
        market_buy_it.save_matching_order(table);
        market_sell_it.save_matching_order(table);
    }

    bool can_match() const  {
        return _can_match;
    }

    order_iterator& maker_it() {
        ASSERT(_can_match);
        return *_maker_it;
    }
    order_iterator& taker_it() {
        ASSERT(_can_match);
        return *_taker_it;
    }

private:
    const dex::symbol_pair_t &_sym_pair;
    match_index_t &_match_index;
    order_iterator limit_buy_it;
    order_iterator limit_sell_it;
    order_iterator market_buy_it;
    order_iterator market_sell_it;

    order_iterator *_taker_it = nullptr;
    order_iterator *_maker_it = nullptr;
    bool _can_match = false;

    void process_data() {
        _taker_it = nullptr;
        _maker_it = nullptr;
        _can_match = false;
        if (market_buy_it.is_valid() && market_sell_it.is_valid()) {
            _taker_it = (market_buy_it.stored_order().order_id < market_sell_it.stored_order().order_id) ? &market_buy_it : &market_sell_it;
            _maker_it = (_taker_it->stored_order().order_side == dex::order_side::BUY) ? &limit_sell_it : &limit_buy_it;
            _can_match = _maker_it->is_valid();
        }
        if (!_can_match) {
            _can_match = true;
            if (market_buy_it.is_valid() && limit_sell_it.is_valid()) {
                _taker_it = &market_buy_it;
                _maker_it = &limit_sell_it;
            } else if (market_sell_it.is_valid() && limit_buy_it.is_valid()) {
                _taker_it = &market_sell_it;
                _maker_it = &limit_buy_it;
            } else if (limit_buy_it.is_valid() && limit_sell_it.is_valid() && limit_buy_it.stored_order().price >= limit_sell_it.stored_order().price) {
                if (limit_buy_it.stored_order().order_id < limit_sell_it.stored_order().order_id) {
                    _taker_it = &limit_buy_it;
                    _maker_it = &limit_sell_it;
                } else {
                    _taker_it = &limit_sell_it;
                    _maker_it = &limit_buy_it;
                }
            } else {
                _can_match = false;
            }
        }

        if (!_can_match) {
            _taker_it = nullptr;
            _maker_it = nullptr;
        }
    }
};

void dex_contract::match_sym_pair(const dex::symbol_pair_t &sym_pair, int32_t &matched_count) {

    if (matched_count >= DEX_MATCH_COUNT_MAX) return; //  meet max matched count

    auto order_tbl = make_order_table(get_self());
    auto match_index = order_tbl.get_index<static_cast<name::raw>(order_match_idx::index_name)>();

    uint64_t sym_pair_id = sym_pair.sym_pair_id;
    std::list<order_t> match_orders;
    // 1. match limit_buy_orders and limit_sell_orders
    auto matching_pair_it = matching_pair_iterator(match_index, sym_pair);
    while (matched_count < DEX_MATCH_COUNT_MAX && matching_pair_it.can_match()) {
        auto &maker_it = matching_pair_it.maker_it();
        auto &taker_it = matching_pair_it.taker_it();

        print("matching taker_order=", maker_it.stored_order(), "\n");
        print("matching maker_order=", taker_it.stored_order(), "\n");

        uint64_t match_price = maker_it.stored_order().price;

        // 2. get match amounts
        uint64_t match_coins = 0;
        uint64_t match_assets = 0;
        get_match_amounts(taker_it.stored_order(), maker_it.stored_order(), match_assets, match_coins);

        // match_orders.push_back(maker_order_store);
        auto &taker_order = taker_it.begin_match();
        auto &maker_order = maker_it.begin_match();

        auto &buy_order = taker_order.order_side == order_side::BUY ? taker_order : maker_order;
        auto &sell_order = taker_order.order_side == order_side::SELL ? taker_order : maker_order;
        // the seller receive coins
        int64_t seller_recv_coins = match_coins;
        // the buyer receive assets
        int64_t buyer_recv_assets = match_assets;
        const symbol &asset_symbol = taker_order.asset_quant.symbol;
        const symbol &coin_symbol = taker_order.coin_quant.symbol;
        // 7. calc match fees
        // 7.1 calc match asset fee payed by buyer
        int64_t asset_match_fee = calc_match_fee(buy_order, _config, taker_order.order_side, buyer_recv_assets);
        if (asset_match_fee > 0) {
            buyer_recv_assets = sub_fee(buyer_recv_assets, asset_match_fee, "buyer_recv_assets");
            // pay the asset_match_fee to payee
            transfer_out(get_self(), _config.bank, _config.payee, asset(asset_match_fee, asset_symbol), "asset_match_fee");
        }

        // 7.2. calc match coin fee payed by seller for exhange
        int64_t coin_match_fee = calc_match_fee(sell_order, _config, taker_order.order_side, seller_recv_coins);
        if (coin_match_fee) {
            seller_recv_coins = sub_fee(seller_recv_coins, coin_match_fee, "seller_recv_coins");
            // pay the coin_match_fee to payee
            transfer_out(get_self(), _config.bank, _config.payee, asset(coin_match_fee, coin_symbol), "coin_match_fee");
        }

        // 8. transfer the coins and assets to seller and buyer
        // 8.1. transfer the coins to seller
        transfer_out(get_self(), _config.bank, sell_order.owner, asset(seller_recv_coins, coin_symbol), "seller_recv_coins");
        // 8.2. transfer the assets to buyer
        transfer_out(get_self(), _config.bank, buy_order.owner, asset(buyer_recv_assets, asset_symbol), "buyer_recv_assets");
        // match sell <-> buy order

        // 9. check order fullfiled to del or update
        // 9.1 check buy order fullfiled to del or update
        buy_order.matched_assets += match_assets;
        buy_order.matched_coins += match_coins;
        if (buy_order.order_type == order_type::MARKET) {
            check(buy_order.matched_coins <= buy_order.coin_quant.amount, "The match coins is overflow for market buy taker order");
            buy_order.is_complete = buy_order.matched_coins == buy_order.coin_quant.amount;
        } else {
            check(buy_order.matched_assets <= buy_order.asset_quant.amount, "The match assets is overflow for sell order");
            buy_order.is_complete = buy_order.matched_assets == buy_order.asset_quant.amount;
        }

        sell_order.matched_assets += match_assets;
        sell_order.matched_coins += match_coins;
        check(sell_order.matched_assets <= sell_order.asset_quant.amount, "The match assets is overflow for sell order");
        sell_order.is_complete = sell_order.matched_assets == sell_order.asset_quant.amount;

        // matching
        check(buy_order.is_complete || sell_order.is_complete, "Neither taker nor maker is finish");

        // TODO: save the matching order detail

        // process refund
        if (buy_order.is_complete)
            process_refund(buy_order);

        matched_count++;

        matching_pair_it.complete_and_next(order_tbl);
    }

    matching_pair_it.save_matching_order(order_tbl);

}