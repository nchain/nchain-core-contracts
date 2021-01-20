#include <dex.hpp>
#include "dex_const.hpp"

using namespace eosio;
using namespace std;
using namespace dex;

inline std::string str_to_upper(string_view str) {
    std::string ret(str.size(), 0);
    for (size_t i = 0; i < str.size(); i++) {
        ret[i] = std::toupper(str[i]);
    }
    return ret;
}

static const std::map<string, pair<order_type_t, order_side_t>> INPUT_ORDER_TYPE_MAP = {
    {"LBO", {order_type::LIMIT, order_side::BUY}},
    {"LSO", {order_type::LIMIT, order_side::SELL}},
    {"MBO", {order_type::MARKET, order_side::BUY}},
    {"MSO", {order_type::MARKET, order_side::SELL}}
};

inline void parse_order_types(string_view str, order_type_t &type, order_side_t &side) {
    auto it = INPUT_ORDER_TYPE_MAP.find(str_to_upper(str));
    CHECK(it != INPUT_ORDER_TYPE_MAP.end(), "Invalid order type=" + string(str))
    type = it->second.first;
    type = it->second.second;
}

inline static uint64_t parse_uint64(string_view str) {
   safe<uint64_t> ret;
   to_int(str, ret);
   return ret.value;
}

uint64_t parse_external_id(string_view str) {
   safe<uint64_t> ret;
   to_int(str, ret);
   return ret.value;
}

void validate_fee_ratio(int64_t ratio, const string &title) {
    CHECK(ratio >= 0 && ratio <= FEE_RATIO_MAX,
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
    CHECK(order_type::is_valid(ret), "invalid order_type=" + string{str});
    return ret;
}

name parse_order_side(string_view str) {
    name ret(str);
    CHECK(order_side::is_valid(ret), "invalid order_side=" + string{str});
    return ret;
}

inline string symbol_to_string(const symbol &s) {
    return std::to_string(s.precision()) + "," + s.code().to_string();
}

inline string symbol_pair_to_string(const symbol &asset_symbol, const symbol &coin_symbol) {
    return symbol_to_string(asset_symbol) + "/" + symbol_to_string(coin_symbol);
}

void dex_contract::setconfig(const dex::config &conf) {
    require_auth( get_self() );
    CHECK(is_account(conf.admin), "The admin account does not exist");
    CHECK(is_account(conf.payee), "The payee account does not exist");
    validate_fee_ratio(conf.maker_ratio, "maker_ratio");
    validate_fee_ratio(conf.taker_ratio, "taker_ratio");

    _conf_tbl.set(conf, get_self());
}

void dex_contract::setsympair(const extended_symbol &asset_symbol,
                              const extended_symbol &coin_symbol, const asset &min_asset_quant,
                              const asset &min_coin_quant, bool only_accept_coin_fee,
                              bool enabled) {
    require_auth( _config.admin );
    const auto &asset_sym = asset_symbol.get_symbol();
    const auto &coin_sym = coin_symbol.get_symbol();
    auto sym_pair_tbl = make_symbol_pair_table(get_self());
    CHECK(is_account(asset_symbol.get_contract()), "The bank account of asset does not exist");
    CHECK(asset_sym.is_valid(), "Invalid asset symbol");
    CHECK(is_account(coin_symbol.get_contract()), "The bank account of coin does not exist");
    CHECK(coin_sym.is_valid(), "Invalid coin symbol");
    CHECK(asset_sym.code() != coin_sym.code(), "Error: asset_symbol.code() == coin_symbol.code()");
    CHECK(asset_sym == min_asset_quant.symbol, "Incorrect symbol of min_asset_quant");
    CHECK(coin_sym == min_coin_quant.symbol, "Incorrect symbol of min_coin_quant");

    auto index = sym_pair_tbl.get_index<static_cast<name::raw>(symbols_idx::index_name)>();
    CHECK( index.find( make_symbols_idx(coin_symbol, asset_symbol) ) == index.end(), "The reverted symbol pair exist");

    auto it = index.find( make_symbols_idx(asset_symbol, coin_symbol));
    if (it == index.end()) {
        // new sym pair
        auto sym_pair_id = _global->new_sym_pair_id();
        CHECK( sym_pair_tbl.find(sym_pair_id) == sym_pair_tbl.end(), "The symbol pair id exist");
        sym_pair_tbl.emplace(get_self(), [&](auto &sym_pair) {
            sym_pair.sym_pair_id          = sym_pair_id;
            sym_pair.asset_symbol         = asset_symbol;
            sym_pair.coin_symbol          = coin_symbol;
            sym_pair.min_asset_quant      = min_asset_quant;
            sym_pair.min_coin_quant       = min_coin_quant;
            sym_pair.only_accept_coin_fee = only_accept_coin_fee;
            sym_pair.enabled              = enabled;
        });
    } else {
        CHECK(it->asset_symbol == asset_symbol, "The asset_symbol mismatch with the existed one");
        CHECK(it->coin_symbol == coin_symbol, "The asset_symbol mismatch with the existed one");
        sym_pair_tbl.modify(*it, same_payer, [&](auto &sym_pair) {
            sym_pair.min_asset_quant      = min_asset_quant;
            sym_pair.min_coin_quant       = min_coin_quant;
            sym_pair.only_accept_coin_fee = only_accept_coin_fee;
            sym_pair.enabled              = enabled;
        });
    }
}

/**
 *  @from: order initiator
 *  @to: DEX contract
 *  @quantity: order initiator to transfer in quantity of either Token A or B to contract
 *  @memo: order transfer memo format:
 *    "<order_type>:<sym_pair_id>:<target_quantity>:<price>:<external_id>[:<taker_ratio>:[maker_ratio]]"
 *     - order_type:
 *          - LBO: Limit  Buy   Order
 *          - LSO: Limit  Sell  Order
 *          - MBO: Market Buy   Order
 *          - MSO: Market Sell  Order
 *
 *   Ex-1: limit buy order
 *       "LBO:1:1.00000000 BTC:2.0000 USD:1"
 *
 *   Ex-2: limit sell order
 *       "LSO:1:1.00000000 BTC:2.0000 USD:1"
 *
 *   Ex-3: market buy order
 *       "MBO:1:2.0000 USD:0.0000 USD:1"
 *
 *   Ex-4: market sell order
 *       "MSO:1:1.00000000 BTC:0.0000 USD:1"
 *
 *   Ex-5: dex operator signed order
 *       "LBO:1:1.00000000 BTC:2.0000 USD:1:8:4"
 */
void dex_contract::ontransfer(name from, name to, asset quantity, string memo) {
    if (from == get_self()) { return; }
    CHECK( to == get_self(), "Must transfer to this contract")
    CHECK( quantity.amount > 0, "The quantity must be positive")

    vector<string_view> params = split(memo, ":");
    CHECK( params.size() > 0 && params[0] == "order", "none-order transfer not supported" )
    CHECK( params.size() == 5 || params.size() == 7, "memo param size must be 7 or 9, instead of " + to_string(params.size()) )
    if (_config.check_order_auth || params.size() == 7) { require_auth(_config.admin); }

    order_t order;
    parse_order_types(params[0], order.order_type, order.order_side);
    order.sym_pair_id = parse_uint64(params[1]);
    order.limit_quant = asset_from_string(params[2]);
    order.price = asset_from_string(params[3]);
    order.external_id = parse_external_id(params[4]);
    if (params.size() == 7) {
        order.taker_ratio = parse_ratio(params[5]);
        order.maker_ratio = parse_ratio(params[6]);
    } else {
        order.taker_ratio = _config.taker_ratio;
        order.maker_ratio = _config.maker_ratio;
    }

    auto sym_pair_tbl = make_symbol_pair_table(get_self());
    auto sym_pair_it = sym_pair_tbl.find(order.sym_pair_id);
    CHECK( sym_pair_it != sym_pair_tbl.end(), "The symbol pair id '" + std::to_string(order.sym_pair_id) + "' does not exist")
    CHECK( sym_pair_it->enabled, "The symbol pair '" + std::to_string(order.sym_pair_id) + " is disabled")

    const auto &asset_symbol = sym_pair_it->asset_symbol.get_symbol();
    const auto &coin_symbol = sym_pair_it->coin_symbol.get_symbol();
    // check price
    CHECK(order.price.symbol == coin_symbol, "The price symbol mismatch with coin_symbol")
    if (order.order_type == order_type::LIMIT) {
        CHECK( order.price.amount > 0, "The price must > 0 for limit order")
    } else { // order.order_type == order_type::LIMIT
        CHECK( order.price.amount == 0, "The price must == 0 for market order")
    }

    auto transfer_bank = get_first_receiver();

    if (order.order_side == order_side::BUY) {
        const auto &expected_bank = sym_pair_it->coin_symbol.get_contract();
        CHECK( transfer_bank == expected_bank, "The transfer bank=" + transfer_bank.to_string() +
                                                    " mismatch with " +
                                                    expected_bank.to_string() + " for buy order");
        CHECK( quantity.symbol == coin_symbol,
                "The transfer quantity symbol=" + symbol_to_string(quantity.symbol) +
                    " mismatch with coin_symbol=" + symbol_to_string(coin_symbol) + " for buy order");

        asset expected_transfer_coins;
        if (order.order_type == order_type::LIMIT) {
            CHECK( order.limit_quant.symbol == asset_symbol,
                    "The limit_quant symbol=" + symbol_to_string(order.limit_quant.symbol) +
                        " mismatch with asset_symbol=" + symbol_to_string(asset_symbol) +
                        " for limit buy order");

            expected_transfer_coins = dex::calc_coin_quant(order.limit_quant, order.price, coin_symbol);
        } else {// order.order_type == order_type::MARKET
            CHECK(order.limit_quant.symbol == coin_symbol,
                    "The limit_quant symbol=" + symbol_to_string(order.limit_quant.symbol) +
                        " mismatch with coin_symbol=" + symbol_to_string(coin_symbol) +
                        " for market buy order");
            expected_transfer_coins = order.limit_quant;
        }
        if (sym_pair_it->only_accept_coin_fee) {
            expected_transfer_coins += dex::calc_match_fee(order.taker_ratio, expected_transfer_coins);
        }
        CHECK( quantity == expected_transfer_coins,
                "The transfer quantity=" + quantity.to_string() + " != expected_transfer_coins=" +
                    expected_transfer_coins.to_string() + " for buy order");

    } else { // order.order_side == order_side::SELL
        const auto &expected_bank = sym_pair_it->asset_symbol.get_contract();
        CHECK(transfer_bank == expected_bank, "The transfer bank=" + transfer_bank.to_string() +
                                                    " mismatch with " +
                                                    expected_bank.to_string() + " for sell order");
        CHECK(quantity.symbol == asset_symbol,
                "The transfer quantity symbol=" + symbol_to_string(quantity.symbol) +
                    " mismatch with " + symbol_to_string(asset_symbol) + " for sell order");
        CHECK(quantity == order.limit_quant, "The transfer quantity=" + quantity.to_string() +
                                                " mismatch with limit_quant=" +
                                                order.limit_quant.to_string() + " for sell order");
    }

    // TODO: need to add the total order coin/asset amount?

    auto order_tbl = make_order_table(get_self());

    order.order_id = _global->new_order_id();
    order.owner = from;
    order.frozen_quant = quantity;
    order.matched_assets = asset(0, asset_symbol);
    order.matched_coins = asset(0, coin_symbol);
    order.matched_fee = (order.order_side == order_side::BUY && !sym_pair_it->only_accept_coin_fee) ?
        order.matched_fee = asset(0, asset_symbol) : asset(0, coin_symbol);

    order.create_time = current_block_time();
    order.status = order_status::MATCHABLE;

    CHECK( order_tbl.find(order.order_id) == order_tbl.end(), "The order exists: order_id=" + std::to_string(order.order_id));

    order_tbl.emplace( get_self(), [&]( auto& o ) {
        o = order;
    });

    if (_config.max_match_count > 0) {
        uint32_t matched_count = 0;
        match_sym_pair(from, *sym_pair_it, _config.max_match_count, matched_count);
    }

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
    CHECK(it != order_tbl.end(), "The order does not exist or has been matched");
    auto order = *it;
    // TODO: support the owner auth to cancel order?
    require_auth(order.owner);

    CHECK(order.status == order_status::MATCHABLE, "The order can not be canceled");

    auto sym_pair_tbl = make_symbol_pair_table(get_self());
    auto sym_pair_it = sym_pair_tbl.find(order.sym_pair_id);
    CHECK( sym_pair_it != sym_pair_tbl.end(),
        "The symbol pair id '" + std::to_string(order.sym_pair_id) + "' does not exist");

    asset quantity;
    name bank;
    if (order.order_side == order_side::BUY) {
        quantity = order.frozen_quant - order.matched_coins;
        bank = sym_pair_it->coin_symbol.get_contract();
    } else { // order.order_side == order_side::SELL
        quantity = order.frozen_quant - order.matched_assets;
        bank = sym_pair_it->asset_symbol.get_contract();
    }
    CHECK(quantity.amount > 0, "Can not unfreeze the invalid quantity=" + quantity.to_string());
    transfer_out(get_self(), bank, order.owner, quantity, "cancel_order");

    order_tbl.modify(it, same_payer, [&]( auto& a ) {
        a.status = order_status::CANCELED;
    });
}

dex::config dex_contract::get_default_config() {
    return {
        get_self(),             // name admin;
        get_self(),             // name payee;
        DEX_MAKER_FEE_RATIO,    // int64_t maker_ratio;
        DEX_TAKER_FEE_RATIO,    // int64_t taker_ratio;
        DEX_MATCH_COUNT_MAX,    // uint32_t max_match_count
        false,                  // bool check_order_auth
    };
}

void dex_contract::match(const name &matcher, uint32_t max_count, const vector<uint64_t> &sym_pairs) {

    CHECK(is_account(matcher), "The matcher account does not exist");
    CHECK(max_count > 0, "The max_count must > 0")
    std::list<symbol_pair_t> sym_pair_list;
    auto sym_pair_tbl = dex::make_symbol_pair_table(get_self());
    if (!sym_pairs.empty()) {
        for (auto sym_pair_id : sym_pairs) {
            auto it = sym_pair_tbl.find(sym_pair_id);
            CHECK(it != sym_pair_tbl.end(), "The symbol pair=" + std::to_string(sym_pair_id) + " does not exist");
            CHECK(it->enabled, "The indicated sym_pair=" + std::to_string(sym_pair_id) + " is disabled");
            sym_pair_list.push_back(*it);
        }
    } else {
        auto sym_pair_it = sym_pair_tbl.begin();
        for (; sym_pair_it != sym_pair_tbl.end(); sym_pair_it++) {
            if (sym_pair_it->enabled) {
                sym_pair_list.push_back(*sym_pair_it);
            }
        }
    }

    uint32_t matched_count = 0;
    for (const auto &sym_pair : sym_pair_list) {
        if (matched_count >= DEX_MATCH_COUNT_MAX) break;

        match_sym_pair(matcher, sym_pair, max_count, matched_count);
    }
    CHECK(matched_count > 0, "The matched count == 0");
}

void dex_contract::match_sym_pair(const name &matcher, const dex::symbol_pair_t &sym_pair,
                                  uint32_t max_count, uint32_t &matched_count) {

    auto cur_block_time = current_block_time();
    auto order_tbl = make_order_table(get_self());
    auto match_index = order_tbl.get_index<static_cast<name::raw>(order_match_idx::index_name)>();

    auto matching_pair_it = dex::matching_pair_iterator(match_index, sym_pair);
    while (matched_count < max_count && matching_pair_it.can_match()) {
        auto &maker_it = matching_pair_it.maker_it();
        auto &taker_it = matching_pair_it.taker_it();

        print("matching taker_order=", maker_it.stored_order(), "\n");
        print("matching maker_order=", taker_it.stored_order(), "\n");

        const auto &matched_price = maker_it.stored_order().price;

        asset matched_coins;
        asset matched_assets;
        matching_pair_it.calc_matched_amounts(matched_assets, matched_coins);
        check(matched_assets.amount > 0 || matched_coins.amount > 0, "Invalid calc_matched_amounts!");
        if (matched_assets.amount == 0 || matched_coins.amount == 0) {
            print("Dust calc_matched_amounts! ", PP0(matched_assets), PP(matched_coins));
        }

        auto &buy_it = (taker_it.order_side() == order_side::BUY) ? taker_it : maker_it;
        auto &sell_it = (taker_it.order_side() == order_side::SELL) ? taker_it : maker_it;

        const auto &buy_order = buy_it.stored_order();
        const auto &sell_order = sell_it.stored_order();
        asset seller_recv_coins = matched_coins;
        asset buyer_recv_assets = matched_assets;
        const auto &asset_symbol = sym_pair.asset_symbol.get_symbol();
        const auto &coin_symbol = sym_pair.coin_symbol.get_symbol();
        const auto &asset_bank = sym_pair.asset_symbol.get_contract();
        const auto &coin_bank = sym_pair.asset_symbol.get_contract();


        asset buy_fee;
        if (matched_coins.symbol == buy_order.matched_fee.symbol) {
            buy_fee = calc_match_fee(buy_order, taker_it.order_side(), matched_coins);
        } else {
            buy_fee = calc_match_fee(buy_order, taker_it.order_side(), buyer_recv_assets);
            buyer_recv_assets -= buy_fee;
        }
        if (buy_fee.amount > 0) {
            // pay the buy_fee to payee
            transfer_out(get_self(), asset_bank, _config.payee, buy_fee, "buy_fee");
        }

        if (buyer_recv_assets.amount > 0) {
            // transfer the assets to buyer
            transfer_out(get_self(), asset_bank, buy_order.owner, buyer_recv_assets, "buyer_recv_assets");
        }

        auto sell_fee = calc_match_fee(sell_order, taker_it.order_side(), seller_recv_coins);
        seller_recv_coins -= sell_fee;
        if (sell_fee.amount > 0) {
            // pay the sell_fee to payee
            transfer_out(get_self(), coin_bank, _config.payee, sell_fee, "sell_fee");
        }
        if (seller_recv_coins.amount > 0) {
            // transfer the coins to seller
            transfer_out(get_self(), coin_bank, sell_order.owner, seller_recv_coins, "seller_recv_coins");
        }

        buy_it.match(matched_assets, matched_coins, buy_fee);
        sell_it.match(matched_assets, matched_coins, sell_fee);

        CHECK(buy_it.is_complete() || sell_it.is_complete(), "Neither buy_order nor sell_order is complete");

        // process refund
        if (buy_it.is_complete()) {
            auto refunds = buy_it.get_refund_coins();
            if (refunds.amount > 0) {
                transfer_out(get_self(), coin_bank, buy_order.owner, refunds, "refund_coins");
            }
        }

        auto deal_tbl = dex::make_deal_table(get_self());
        deal_tbl.emplace(matcher, [&]( auto& deal_item ) {
            deal_item.id = _global->new_deal_item_id();
            deal_item.buy_order_id = buy_order.order_id;
            deal_item.sell_order_id = sell_order.order_id;
            deal_item.deal_assets = matched_assets;
            deal_item.deal_coins = matched_coins;
            deal_item.deal_price = matched_price;
            deal_item.taker_side = taker_it.order_side();
            deal_item.buy_fee = buy_fee;
            deal_item.sell_fee = sell_fee;
            deal_item.deal_time = cur_block_time;
            print("The matched deal_item=", deal_item, "\n");
        });

        matched_count++;
        matching_pair_it.complete_and_next(order_tbl);
    }

    matching_pair_it.save_matching_order(order_tbl);
}