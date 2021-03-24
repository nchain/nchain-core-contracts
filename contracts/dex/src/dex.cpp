#include <dex.hpp>
#include "dex_const.hpp"
#include "eosio.token.hpp"
#include "version.hpp"

using namespace eosio;
using namespace std;
using namespace dex;

#define CHECK_DEX_ENABLED() { \
    CHECK(_config.dex_enabled, string("DEX is disabled! function=") + __func__); \
}

inline std::string str_to_upper(string_view str) {
    std::string ret(str.size(), 0);
    for (size_t i = 0; i < str.size(); i++) {
        ret[i] = std::toupper(str[i]);
    }
    return ret;
}

inline static uint64_t parse_uint64(string_view str) {
   safe<uint64_t> ret;
   to_int(str, ret);
   return ret.value;
}

void validate_fee_ratio(int64_t ratio, const string &title) {
    CHECK(ratio >= 0 && ratio <= FEE_RATIO_MAX,
          "The " + title + " out of range [0, " + std::to_string(FEE_RATIO_MAX) + "]");
}

inline string symbol_to_string(const symbol &s) {
    return std::to_string(s.precision()) + "," + s.code().to_string();
}

inline string symbol_pair_to_string(const symbol &asset_symbol, const symbol &coin_symbol) {
    return symbol_to_string(asset_symbol) + "/" + symbol_to_string(coin_symbol);
}

ACTION dex_contract::init() {
    // order_tbl orders(_self, _self.value);

    // auto s = orders.begin();
	// while(s != orders.end()){
	// 	s = orders.erase(s);
	// }

    // _conf_tbl.remove();
    
    // deal_table deals(_self, _self.value);
    // auto t = deals.begin();
    // while(t != deals.end()){
	// 	t = deals.erase(t);
	// }

}

void dex_contract::setconfig(const dex::config &conf) {
    require_auth( get_self() );
    CHECK( is_account(conf.dex_admin), "The dex_admin account does not exist");
    CHECK( is_account(conf.dex_fee_collector), "The dex_fee_collector account does not exist");
    validate_fee_ratio( conf.maker_fee_ratio, "maker_fee_ratio");
    validate_fee_ratio( conf.taker_fee_ratio, "taker_fee_ratio");

    _conf_tbl.set(conf, get_self());
}

void dex_contract::setsympair(const extended_symbol &asset_symbol,
                              const extended_symbol &coin_symbol, const asset &min_asset_quant,
                              const asset &min_coin_quant, bool only_accept_coin_fee,
                              bool enabled) {
    require_auth( _config.dex_admin );
    const auto &asset_sym = asset_symbol.get_symbol();
    const auto &coin_sym = coin_symbol.get_symbol();
    auto sympair_tbl = make_sympair_table(get_self());
    CHECK(is_account(asset_symbol.get_contract()), "The bank account of asset does not exist");
    CHECK(asset_sym.is_valid(), "Invalid asset symbol");
    CHECK(is_account(coin_symbol.get_contract()), "The bank account of coin does not exist");
    CHECK(coin_sym.is_valid(), "Invalid coin symbol");
    CHECK(asset_sym.code() != coin_sym.code(), "Error: asset_symbol.code() == coin_symbol.code()");
    CHECK(asset_sym == min_asset_quant.symbol, "Incorrect symbol of min_asset_quant");
    CHECK(coin_sym == min_coin_quant.symbol, "Incorrect symbol of min_coin_quant");

    auto index = sympair_tbl.get_index<static_cast<name::raw>(symbols_idx::index_name)>();
    CHECK( index.find( make_symbols_idx(coin_symbol, asset_symbol) ) == index.end(), "The reverted symbol pair exist");

    auto it = index.find( make_symbols_idx(asset_symbol, coin_symbol));
    if (it == index.end()) {
        // new sym pair
        auto sympair_id = _global->new_sympair_id();
        CHECK( sympair_tbl.find(sympair_id) == sympair_tbl.end(), "The symbol pair id exist");
        sympair_tbl.emplace(get_self(), [&](auto &sym_pair) {
            sym_pair.sympair_id          = sympair_id;
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
        sympair_tbl.modify(*it, same_payer, [&](auto &sym_pair) {
            sym_pair.min_asset_quant      = min_asset_quant;
            sym_pair.min_coin_quant       = min_coin_quant;
            sym_pair.only_accept_coin_fee = only_accept_coin_fee;
            sym_pair.enabled              = enabled;
        });
    }
}

void dex_contract::onoffsympair(const uint64_t& sympair_id, const bool& on_off) {
    require_auth( _config.dex_admin );

    auto sympair_tbl = make_sympair_table(_self);
    auto it = sympair_tbl.find(sympair_id);
    CHECK( it != sympair_tbl.end(), "sympair not found: " + to_string(sympair_id) )
    sympair_tbl.modify(*it, same_payer, [&](auto &row) {
        row.enabled                 = on_off;
    });
}

void dex_contract::ontransfer(const name& from, const name& to, const asset& quant, const string& memo) {
    CHECK_DEX_ENABLED()
    if (from == get_self()) { return; }
    CHECK( to == get_self(), "Must transfer to this contract")
    CHECK( quant.amount > 0, "The quantity must be positive")
    add_balance(from, get_first_receiver(), quant, get_self());
}

void dex_contract::withdraw(const name &user, const name &to, const name &token_code, const asset& quant, const string &memo) {
    CHECK_DEX_ENABLED()
    require_auth(user);
    check(quant.amount > 0, "quantity must be positive");

    auto account_tbl = make_account_table(get_self(), user);
    auto index = account_tbl.get_index<static_cast<name::raw>(account_sym_idx::index_name)>();
    auto it = index.find( make_uint128(token_code.value, quant.symbol.raw()) );
    CHECK(it != index.end(),
          "the balance does not exist! user=" + user.to_string() +
              ", token_code=" + token_code.to_string() +
              ", sym=" + symbol_to_string(quant.symbol));

    ASSERT(it->balance.contract == token_code);

    index.modify(it, same_payer, [&]( auto& a ) {
        a.balance.quantity -= quant;
        CHECK(it->balance.quantity.amount >= 0, "insufficient funds");
    });

    TRANSFER( token_code, user, quant, "withdraw" )
}

void dex_contract::cancel(const uint64_t &order_id) {
    CHECK_DEX_ENABLED()
    auto order_tbl = make_order_table(get_self());
    auto it = order_tbl.find(order_id);
    CHECK(it != order_tbl.end(), "The order does not exist or has been matched");
    auto order = *it;
    // TODO: support the owner auth to cancel order?
    require_auth(order.owner);

    CHECK(order.status == order_status::MATCHABLE, "The order can not be canceled");

    auto sympair_tbl = make_sympair_table(get_self());
    auto sym_pair_it = sympair_tbl.find(order.sympair_id);
    CHECK( sym_pair_it != sympair_tbl.end(),
        "The symbol pair id '" + std::to_string(order.sympair_id) + "' does not exist");
    CHECK( sym_pair_it->enabled, "The symbol pair '" + std::to_string(order.sympair_id) + " is disabled")

    asset quantity;
    name bank;
    if (order.order_side == order_side::BUY) {
        quantity = order.frozen_quant - order.matched_coins;
        bank = sym_pair_it->coin_symbol.get_contract();
    } else { // order.order_side == order_side::SELL
        quantity = order.frozen_quant - order.matched_assets;
        bank = sym_pair_it->asset_symbol.get_contract();
    }
    CHECK(quantity.amount >= 0, "Can not unfreeze the invalid quantity=" + quantity.to_string());
    if (quantity.amount > 0) {
        add_balance(order.owner, bank, quantity, order.owner);
    }

    order_tbl.modify(it, same_payer, [&]( auto& a ) {
        a.status = order_status::CANCELED;
        a.last_updated_at = current_block_time();
    });
}

dex::config dex_contract::get_default_config() {
    return {
        true,                   // bool dex_enabled
        get_self(),             // name admin;
        get_self(),             // name dex_fee_collector;
        DEX_MAKER_FEE_RATIO,    // int64_t maker_fee_ratio;
        DEX_TAKER_FEE_RATIO,    // int64_t taker_fee_ratio;
        DEX_MATCH_COUNT_MAX,    // uint32_t max_match_count
        false,                  // bool admin_sign_required
        DATA_RECYCLE_SEC,       // int64_t old_data_outdate_secs
    };
}

void dex_contract::match(const name &matcher, uint32_t max_count, const vector<uint64_t> &sym_pairs, const string &memo) {
    CHECK_DEX_ENABLED()

    CHECK(is_account(matcher), "The matcher account does not exist");
    CHECK(max_count > 0, "The max_count must > 0")
    std::list<symbol_pair_t> sym_pair_list;
    auto sympair_tbl = dex::make_sympair_table(get_self());
    if (!sym_pairs.empty()) {
        for (auto sympair_id : sym_pairs) {
            auto it = sympair_tbl.find(sympair_id);
            CHECK(it != sympair_tbl.end(), "The symbol pair=" + std::to_string(sympair_id) + " does not exist");
            CHECK(it->enabled, "The indicated sym_pair=" + std::to_string(sympair_id) + " is disabled");
            sym_pair_list.push_back(*it);
        }
    } else {
        auto sym_pair_it = sympair_tbl.begin();
        for (; sym_pair_it != sympair_tbl.end(); sym_pair_it++) {
            if (sym_pair_it->enabled) {
                sym_pair_list.push_back(*sym_pair_it);
            }
        }
    }

    uint32_t matched_count = 0;
    for (const auto &sym_pair : sym_pair_list) {
        if (matched_count >= DEX_MATCH_COUNT_MAX) break;

        match_sym_pair(matcher, sym_pair, max_count, matched_count, memo);
    }

    CHECK(matched_count > 0, "None matched");
}

void dex_contract::match_sym_pair(const name &matcher, const dex::symbol_pair_t &sym_pair,
                                  uint32_t max_count, uint32_t &matched_count, const string &memo) {

    auto cur_block_time = current_block_time();
    auto order_tbl = make_order_table(get_self());
    auto match_index = order_tbl.get_index<static_cast<name::raw>(order_match_idx::index_name)>();

    auto matching_pair_it = dex::matching_pair_iterator(match_index, sym_pair);
    while (matched_count < max_count && matching_pair_it.can_match()) {
        auto &maker_it = matching_pair_it.maker_it();
        auto &taker_it = matching_pair_it.taker_it();

        TRACE_L("matching taker_order=", maker_it.stored_order());
        TRACE_L("matching maker_order=", taker_it.stored_order());

        const auto &matched_price = maker_it.stored_order().price;

        asset matched_coins;
        asset matched_assets;
        matching_pair_it.calc_matched_amounts(matched_assets, matched_coins);
        check(matched_assets.amount > 0 || matched_coins.amount > 0, "Invalid calc_matched_amounts!");
        if (matched_assets.amount == 0 || matched_coins.amount == 0) {
            TRACE_L("Dust calc_matched_amounts! ", PP0(matched_assets), PP(matched_coins));
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
        const auto &coin_bank = sym_pair.coin_symbol.get_contract();


        asset buy_fee;
        // transfer the buy_fee from buy_order to dex_fee_collector
        if (matched_coins.symbol == buy_order.matched_fee.symbol) {
            buy_fee = calc_match_fee(buy_order, taker_it.order_side(), matched_coins);
            add_balance(_config.dex_fee_collector, coin_bank, buy_fee, get_self());
        } else {
            buy_fee = calc_match_fee(buy_order, taker_it.order_side(), buyer_recv_assets);
            buyer_recv_assets -= buy_fee;
            add_balance(_config.dex_fee_collector, asset_bank, buy_fee, get_self());
        }

        auto sell_fee = calc_match_fee(sell_order, taker_it.order_side(), seller_recv_coins);
        seller_recv_coins -= sell_fee;
        // transfer the sell_fee from sell_order to dex_fee_collector
        add_balance(_config.dex_fee_collector, coin_bank, sell_fee, get_self());

        // transfer the coins from buy_order to seller
        add_balance(sell_order.owner, coin_bank, seller_recv_coins, get_self());

        // transfer the assets from sell_order  to buyer
        add_balance(buy_order.owner, asset_bank, buyer_recv_assets, get_self());

        auto deal_id = _global->new_deal_item_id();

        buy_it.match(deal_id, matched_assets, matched_coins, buy_fee);
        sell_it.match(deal_id, matched_assets, matched_coins, sell_fee);

        CHECK(buy_it.is_completed() || sell_it.is_completed(), "Neither buy_order nor sell_order is completed");

        // process refund
        asset buy_refund_coins(0, coin_symbol);
        if (buy_it.is_completed()) {
            buy_refund_coins = buy_it.get_refund_coins();
            if (buy_refund_coins.amount > 0) {
                // refund from buy_order to buyer
                add_balance(buy_order.owner, coin_bank, buy_refund_coins, get_self());
            }
        }
        auto deal_tbl = dex::make_deal_table(get_self());
        deal_tbl.emplace(matcher, [&]( auto& deal_item ) {
            deal_item.id = deal_id;
            deal_item.sympair_id = sym_pair.sympair_id;
            deal_item.buy_order_id = buy_order.order_id;
            deal_item.sell_order_id = sell_order.order_id;
            deal_item.deal_assets = matched_assets;
            deal_item.deal_coins = matched_coins;
            deal_item.deal_price = matched_price;
            deal_item.taker_side = taker_it.order_side();
            deal_item.buy_fee = buy_fee;
            deal_item.sell_fee = sell_fee;
            deal_item.buy_refund_coins = buy_refund_coins;
            deal_item.memo = memo;
            deal_item.deal_time = cur_block_time;
            TRACE_L("The matched deal_item=", deal_item);
        });

        matched_count++;
        matching_pair_it.complete_and_next(order_tbl);
    }

    matching_pair_it.save_matching_order(order_tbl);
}

void dex_contract::version() {
    CHECK(false, "version: " + dex::version());
}

void dex_contract::neworder(const name &user, const uint64_t &sympair_id, const name &order_type,
                            const name &order_side, const asset &limit_quant,
                            const asset &frozen_quant, const asset &price,
                            const uint64_t &external_id,
                            const optional<dex::order_config_ex_t> &order_config_ex) {
    // frozen_quant not in use
    new_order(user, sympair_id, order_type, order_side, limit_quant, price,
              external_id, order_config_ex);
}

void dex_contract::new_order(const name &user, const uint64_t &sympair_id, const name &order_type,
                             const name &order_side, const asset &limit_quant,
                             const optional<asset> &price,
                             const uint64_t &external_id,
                             const optional<dex::order_config_ex_t> &order_config_ex) {
    CHECK_DEX_ENABLED()
    CHECK(is_account(user), "Account of user=" + user.to_string() + " does not existed");
    require_auth(user);
    if (_config.admin_sign_required || order_config_ex) { require_auth(_config.dex_admin); }

    auto sympair_tbl = make_sympair_table(get_self());
    auto sym_pair_it = sympair_tbl.find(sympair_id);
    CHECK( sym_pair_it != sympair_tbl.end(), "The symbol pair id '" + std::to_string(sympair_id) + "' does not exist")
    CHECK( sym_pair_it->enabled, "The symbol pair '" + std::to_string(sympair_id) + " is disabled")

    const auto &asset_symbol = sym_pair_it->asset_symbol.get_symbol();
    const auto &coin_symbol = sym_pair_it->coin_symbol.get_symbol();

    auto taker_fee_ratio = _config.taker_fee_ratio;
    auto maker_fee_ratio = _config.maker_fee_ratio;
    if (order_config_ex) {
        taker_fee_ratio = order_config_ex->taker_fee_ratio;
        maker_fee_ratio = order_config_ex->maker_fee_ratio;
        validate_fee_ratio(taker_fee_ratio, "ratio");
        validate_fee_ratio(maker_fee_ratio, "ratio");
    }

    // check price
    if (price) {
        CHECK(price->symbol == coin_symbol, "The price symbol mismatch with coin_symbol")
        if (order_type == dex::order_type::LIMIT) {
            CHECK( price->amount > 0, "The price must > 0 for limit order")
        } else { // order.order_type == dex::order_type::LIMIT
            CHECK( price->amount == 0, "The price must == 0 for market order")
        }
    }

    asset frozen_quant;
    if (order_side == dex::order_side::BUY) {
        if (order_type == dex::order_type::LIMIT) {
            CHECK( limit_quant.symbol == asset_symbol,
                    "The limit_symbol=" + symbol_to_string(limit_quant.symbol) +
                        " mismatch with asset_symbol=" + symbol_to_string(asset_symbol) +
                        " for limit buy order");
            ASSERT(price.has_value());
            frozen_quant = dex::calc_coin_quant(limit_quant, *price, coin_symbol);
        } else {// order_type == order_type::MARKET
            CHECK(limit_quant.symbol == coin_symbol,
                    "The limit_symbol=" + symbol_to_string(limit_quant.symbol) +
                        " mismatch with coin_symbol=" + symbol_to_string(coin_symbol) +
                        " for market buy order");
            frozen_quant = limit_quant;
        }
        if (sym_pair_it->only_accept_coin_fee) {
            frozen_quant += dex::calc_match_fee(taker_fee_ratio, frozen_quant);
        }

    } else { // order_side == order_side::SELL
        CHECK( limit_quant.symbol == asset_symbol,
                "The limit_symbol=" + symbol_to_string(limit_quant.symbol) +
                    " mismatch with asset_symbol=" + symbol_to_string(asset_symbol) +
                    " for sell order");
        frozen_quant = limit_quant;
    }

    auto order_tbl = make_order_table(get_self());

    const auto &fee_symbol = (order_side == dex::order_side::BUY && !sym_pair_it->only_accept_coin_fee) ?
            asset_symbol : coin_symbol;

    auto order_id = _global->new_order_id();
    CHECK( order_tbl.find(order_id) == order_tbl.end(), "The order exists: order_id=" + std::to_string(order_id));

    auto cur_block_time = current_block_time();
    order_tbl.emplace(get_self(), [&](auto &order) {
        order.order_id = order_id;
        order.external_id = external_id;
        order.owner = user;
        order.sympair_id = sympair_id;
        order.order_type = order_type;
        order.order_side = order_side;
        order.price = price ? *price : asset(0, coin_symbol);
        order.limit_quant = limit_quant;
        order.frozen_quant = frozen_quant;
        order.taker_fee_ratio = taker_fee_ratio;
        order.maker_fee_ratio = maker_fee_ratio;
        order.matched_assets = asset(0, asset_symbol);
        order.matched_coins = asset(0, coin_symbol);
        order.matched_fee = asset(0, fee_symbol);
        order.status = order_status::MATCHABLE;
        order.created_at = cur_block_time;
        order.last_updated_at = cur_block_time;
        order.last_deal_id = 0;
    });


    name frozen_bank = (order_side == dex::order_side::BUY) ? sym_pair_it->coin_symbol.get_contract() :
            sym_pair_it->asset_symbol.get_contract();

    sub_balance(user, frozen_bank, frozen_quant, user);

    if (_config.max_match_count > 0) {
        uint32_t matched_count = 0;
        match_sym_pair(get_self(), *sym_pair_it, _config.max_match_count, matched_count, "oid:" + std::to_string(order_id));
    }
}

void dex_contract::add_balance(const name &user, const name &bank, const asset &quantity, const name &ram_payer) {
    auto account_tbl = make_account_table(get_self(), user);

    auto index = account_tbl.get_index<static_cast<name::raw>(account_sym_idx::index_name)>();
    auto it = index.find( make_uint128(bank.value, quantity.symbol.raw()) );
    if (it == index.end()) {
        CHECK(quantity.amount >= 0, "the balance does not exist! user=" + user.to_string() +
              ", bank=" + bank.to_string() +
              ", sym=" + symbol_to_string(quantity.symbol));
        // create balance of account
        auto id = account_tbl.available_primary_key();
        TRACE_L("create balance. id=", id, ", account=", user.to_string(), ", bank=", bank.to_string(),
            ", quantity=", quantity);
        account_tbl.emplace( ram_payer, [&]( auto& a ) {
            a.id = id; // TODO: add auto-inc account_id in global
            a.balance.contract = bank;
            a.balance.quantity = quantity;
        });
    } else {
        TRACE_L("add balance. id=", it->id, ", account=", user.to_string(), ", bank=", bank.to_string(),
            ", quantity=", quantity);
        ASSERT(it->balance.contract == bank);
        index.modify(it, same_payer, [&]( auto& a ) {
            a.balance.quantity += quantity;
            CHECK(it->balance.quantity.amount >= 0, "insufficient balance of user=" + user.to_string());
        });
    }
    return;
}

void dex_contract::buymarket(const name &user, const uint64_t &sympair_id, const asset &coins,
                             const uint64_t &external_id,
                             const optional<dex::order_config_ex_t> &order_config_ex) {
    new_order(user, sympair_id, order_type::MARKET, order_side::BUY, coins, nullopt,
              external_id, order_config_ex);
}

void dex_contract::sellmarket(const name &user, const uint64_t &sympair_id, const asset &quantity,
                              const uint64_t &external_id,
                              const optional<dex::order_config_ex_t> &order_config_ex) {
    new_order(user, sympair_id, order_type::MARKET, order_side::SELL, quantity, nullopt,
              external_id, order_config_ex);
}

void dex_contract::buylimit(const name &user, const uint64_t &sympair_id, const asset &quantity,
                            const asset &price, const uint64_t &external_id,
                            const optional<dex::order_config_ex_t> &order_config_ex) {
    new_order(user, sympair_id, order_type::LIMIT, order_side::BUY, quantity, price,
              external_id, order_config_ex);
}

void dex_contract::selllimit(const name &user, const uint64_t &sympair_id, const asset &quantity,
                             const asset &price, const uint64_t &external_id,
                             const optional<dex::order_config_ex_t> &order_config_ex) {
    new_order(user, sympair_id, order_type::LIMIT, order_side::SELL, quantity, price,
              external_id, order_config_ex);
}

bool dex_contract::check_data_outdated(const time_point &data_time, const time_point &now) {
    ASSERT(now.sec_since_epoch() >= data_time.sec_since_epoch());
    uint64_t sec = now.sec_since_epoch() - data_time.sec_since_epoch();
    return sec > _config.data_recycle_sec;
}

void dex_contract::cleandata(const uint64_t &max_count) {
    CHECK_DEX_ENABLED()
    auto cur_block_time = current_block_time();

    auto deal_tbl = make_deal_table(get_self());
    auto order_tbl = make_order_table(get_self());
    auto deal_it = deal_tbl.begin();

    uint64_t count = 0, related_count = 0;
    while (count < max_count && deal_it != deal_tbl.end() &&
           check_data_outdated(deal_it->deal_time, cur_block_time)) {
        // erase buy order
        auto buy_it = order_tbl.find(deal_it->buy_order_id);
        if (buy_it != order_tbl.end() && buy_it->status == order_status::COMPLETED &&
            buy_it->last_deal_id == deal_it->id) {

            TRACE_L("Erase buy order=", buy_it->order_id, " of deal_item=", deal_it->id);
            order_tbl.erase(buy_it);
            related_count++;
        }
        // erase sell order
        auto sell_it = order_tbl.find(deal_it->sell_order_id);
        if (sell_it != order_tbl.end() && sell_it->status == order_status::COMPLETED &&
            sell_it->last_deal_id == deal_it->id) {

            TRACE_L("Erase sell order=", sell_it->order_id, " of deal_item=", deal_it->id);
            order_tbl.erase(sell_it);
            related_count++;
        }
        TRACE_L("Erase deal_item=", deal_it->id);
        deal_it = deal_tbl.erase(deal_it);
        count++;
    }

    auto order_index = order_tbl.get_index<static_cast<name::raw>(order_updated_at_idx::index_name)>();
    if (count < max_count) {
        auto canceled_order_it = order_index.upper_bound(make_uint128(order_status::CANCELED.value, 0));
        while (count < max_count && canceled_order_it != order_index.end() &&
               canceled_order_it->status == order_status::CANCELED &&
               check_data_outdated(deal_it->deal_time, cur_block_time)) {
            TRACE_L("Erase canceled order=", canceled_order_it->order_id);
            canceled_order_it = order_index.erase(canceled_order_it);
            count++;
        }
    }

    if (count < max_count) {
        auto completed_order_it = order_index.upper_bound(make_uint128(order_status::COMPLETED.value, 0));
        while(count < max_count && completed_order_it != order_index.end() &&
                completed_order_it->status == order_status::COMPLETED &&
                check_data_outdated(deal_it->deal_time, cur_block_time)) {
            TRACE_L("Erase completed order=", completed_order_it->order_id);
            completed_order_it = order_index.erase(completed_order_it);
            count++;
        }
    }
    CHECK(count > 0, "No data to be cleaned");
    TRACE_L("Found and erased item count=", count, ", related_count=", related_count);
}
