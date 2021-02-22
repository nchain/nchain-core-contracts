#pragma once

#include "dex_states.hpp"
#include <utils.hpp>

namespace dex {

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
        CHECK(digit >= 0 && digit <= 18, "precision digit " + std::to_string(digit) + " should be in range[0,18]");
        return power10(digit);
    }

    int64_t calc_asset_amount(const asset &coin_quant, const asset &price, const symbol &asset_symbol) {
        ASSERT(coin_quant.symbol.precision() == price.symbol.precision());
        int128_t precision = calc_precision(asset_symbol.precision());
        return divide_decimal64(coin_quant.amount, price.amount, precision);
    }

    int64_t calc_coin_amount(const asset &asset_quant, const asset &price, const symbol &coin_symbol) {
        ASSERT(coin_symbol.precision() == price.symbol.precision());
        int128_t precision = calc_precision(asset_quant.symbol.precision());
        return multiply_decimal64(asset_quant.amount, price.amount, precision);
    }

    asset calc_asset_quant(const asset &coin_quant, const asset &price, const symbol &asset_symbol) {
        return asset(calc_asset_amount(coin_quant, price, asset_symbol), asset_symbol);
    }

    asset calc_coin_quant(const asset &asset_quant, const asset &price, const symbol &coin_symbol) {
        return asset(calc_coin_amount(asset_quant, price, coin_symbol), coin_symbol);
    }

    inline asset calc_match_fee(int64_t ratio, const asset &quant) {
        if (quant.amount == 0) return asset{0, quant.symbol};
        int64_t fee = multiply_decimal64(quant.amount, ratio, RATIO_PRECISION);
        CHECK(fee < quant.amount, "the calc_fee is large than quantity=" + quant.to_string() + ", ratio=" + to_string(ratio));
        return asset{fee, quant.symbol};
    }

    inline asset calc_match_fee(const dex::order_t &order, const order_type_t &taker_side, const asset &quant) {
        int64_t ratio = (order.order_side == taker_side) ? order.taker_fee_ratio : order.maker_fee_ratio;
        return calc_match_fee(ratio, quant);
    }

    template<typename match_index_t>
    class matching_order_iterator {
    public:
        enum status_t {
            CLOSED,
            OPENED,
            MATCHING,
            COMPLETED
        };
    public:
        matching_order_iterator(match_index_t &match_index, uint64_t sympair_id, order_side_t side,
                                order_type_t type)
            : _match_index(match_index), _it(match_index.end()), _sym_pair_id(sympair_id),
              _order_side(side), _order_type(type) {

            TRACE("creating matching order itr! sympair_id=", _sym_pair_id, ", side=", _order_side, ", type=", _order_type, "\n");
            if (_order_side == order_side::BUY) {
                _key = make_order_match_idx(_sym_pair_id, order_status::MATCHABLE, _order_side, _order_type,
                                           std::numeric_limits<uint64_t>::max(), 0);
            } else { // _order_side == order_side::SELL
                _key = make_order_match_idx(_sym_pair_id, order_status::MATCHABLE, _order_side, _order_type, 0, 0);
            }
            _it       = _match_index.upper_bound(_key);
            process_data();
        };

        template<typename table_t>
        void complete_and_next(table_t &table) {
            ASSERT(is_completed());
            const auto &store_order = *_it;
            _it++;
            table.modify(store_order, same_payer, [&]( auto& a ) {
                a.matched_assets = _matched_assets;
                a.matched_coins = _matched_coins;
                a.matched_fee = _matched_fee;
                a.status = order_status::COMPLETED;
                a.last_updated_at = current_block_time();
                a.last_deal_id = _last_deal_id;
            });
            process_data();
        }

        template<typename table_t>
        void save_matching_order(table_t &table) {
            if (is_matching()) {
                table.modify(*_it, same_payer, [&]( auto& a ) {
                    a.matched_assets = _matched_assets;
                    a.matched_coins = _matched_coins;
                    a.matched_fee = _matched_fee;
                    a.last_updated_at = current_block_time();
                    a.last_deal_id = _last_deal_id;
                });
            }
        }

        inline const order_t &stored_order() {
            ASSERT(is_valid());
            return *_it;
        }

        inline void match(uint64_t deal_id, const asset &new_matched_assets, const asset &new_matched_coins, const asset &new_matched_fee) {
            ASSERT(_status == OPENED || _status == MATCHING);
            if (_status == OPENED) {
                _status = MATCHING;
            }
            bool completed = false;

            _last_deal_id = deal_id;
            _matched_assets += new_matched_assets;
            _matched_coins += new_matched_coins;
            _matched_fee += new_matched_fee;
            const auto &order = *_it;
            if (order.order_side == order_side::BUY && order.order_type == order_type::MARKET) {
                    CHECK(_matched_coins <= order.limit_quant,
                        "The matched coins=" + _matched_coins.to_string() +
                        " is overflow with limit_quant=" + order.limit_quant.to_string() + " for market buy order");
                    completed = _matched_coins == order.limit_quant;
            } else {
                CHECK(_matched_assets <= order.limit_quant,
                    "The matched assets=" + _matched_assets.to_string() +
                    " is overflow with limit_quant=" + order.limit_quant.to_string());
                completed = _matched_assets == order.limit_quant;
            }

            if (order.order_side == order_side::BUY) {
                auto total_matched_coins = (_matched_coins.symbol == _matched_fee.symbol) ?
                    _matched_coins + _matched_fee // the buyer pay fee with coins
                    : _matched_coins;
                CHECK(total_matched_coins <= order.frozen_quant,
                        "The total_matched_coins=" + _matched_coins.to_string() +
                        " is overflow with frozen_quant=" + order.frozen_quant.to_string() + " for buy order");
                if (completed) {
                    _status = COMPLETED;
                    if (order.order_type == order_type::LIMIT) {
                        if (order.frozen_quant > total_matched_coins) {
                            _refund_coins = order.frozen_quant - total_matched_coins;
                        }
                    }
                }
            }

            if (completed) {
                _status = COMPLETED;
            }
        }

        inline bool is_valid() const {
            return _status != CLOSED;
        }

        inline bool is_completed() const {
            return _status == COMPLETED;
        }

        inline bool is_matching() const {
            return _status == MATCHING;
        }

        inline asset get_free_limit_quant() const {
            ASSERT(is_valid());
            asset ret;
            if (_it->order_side == order_side::BUY && _it->order_type == order_type::MARKET) {
                ret = _it->limit_quant - _matched_coins;
            } else {
                ret = _it->limit_quant - _matched_assets;
            }
            ASSERT(ret.amount >= 0);
            return ret;
        }

        inline asset get_refund_coins() const {
            ASSERT(is_completed());
            return _refund_coins;
        }

        const order_side_t &order_side() const {
            return _order_side;
        }

        const order_type_t &order_type() const {
            return _order_type;
        }

    private:
        void process_data() {
            _status = CLOSED;
            if (_it == _match_index.end()) {
                TRACE("matching order itr end! sympair_id=", _sym_pair_id, ", side=", _order_side, ", type=", _order_type, "\n");
                return;
            }

            const auto &stored_order = *_it;
            CHECK(_key < stored_order.get_order_match_idx(), "the start key must < found order key");
            // TRACE("start key=", key, ", found key=", stored_order.get_order_match_idx(), "\n");

            if (stored_order.sympair_id != _sym_pair_id || stored_order.status != order_status::MATCHABLE ||
                stored_order.order_side != _order_side || stored_order.order_type != _order_type) {
                return;
            }
            TRACE("found order! order=", stored_order, "\n");

            _last_deal_id = stored_order.last_deal_id;
            _matched_assets = stored_order.matched_assets;
            _matched_coins  = stored_order.matched_coins;
            _matched_fee  = stored_order.matched_fee;
            _refund_coins = asset(0, _matched_coins.symbol);
            _status = OPENED;
        }

        match_index_t &_match_index;
        order_match_idx_key _key;
        typename match_index_t::const_iterator _it;
        uint64_t _sym_pair_id;
        order_type_t _order_type;
        order_side_t _order_side;
        status_t _status = CLOSED;

        uint64_t _last_deal_id = 0;
        asset _matched_assets;      //!< total matched asset amount
        asset _matched_coins;       //!< total matched coin amount
        asset _matched_fee;        //!< total matched fees
        asset _refund_coins;

    };


    template<typename match_index_t>
    class matching_pair_iterator {
    public:
        using order_iterator = matching_order_iterator<match_index_t>;
        using order_iterator_vector = std::vector<order_iterator*>;

        matching_pair_iterator(match_index_t &match_index, const dex::symbol_pair_t &sym_pair)
            : _match_index(match_index), _sym_pair(sym_pair),
            limit_buy_it(match_index, sym_pair.sympair_id, order_side::BUY, order_type::LIMIT),
            limit_sell_it(match_index, sym_pair.sympair_id, order_side::SELL, order_type::LIMIT),
            market_buy_it(match_index, sym_pair.sympair_id, order_side::BUY, order_type::MARKET),
            market_sell_it(match_index, sym_pair.sympair_id, order_side::SELL, order_type::MARKET) {

            process_data();
        }

        template<typename table_t>
        void complete_and_next(table_t &table) {
            if (_taker_it->is_completed()) {
                _taker_it->complete_and_next(table);
            }
            if (_maker_it->is_completed()) {
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

        void calc_matched_amounts(asset &matched_assets, asset &matched_coins) {
            const auto &asset_symbol = _sym_pair.asset_symbol.get_symbol();
            const auto &coin_symbol = _sym_pair.coin_symbol.get_symbol();
            ASSERT(_maker_it->order_type() == order_type::LIMIT && _maker_it->stored_order().price.amount > 0);

            const auto &matched_price = _maker_it->stored_order().price;

            auto maker_free_assets = _maker_it->get_free_limit_quant();
            ASSERT(maker_free_assets.symbol == asset_symbol);
            CHECK(maker_free_assets.amount > 0, "MUST: maker_free_assets > 0");

            asset taker_free_assets;
            if (_taker_it->order_side() == order_side::BUY && _taker_it->order_type() == order_type::MARKET) {
                auto taker_free_coins = _taker_it->get_free_limit_quant();
                ASSERT(taker_free_coins.symbol == coin_symbol);
                CHECK(taker_free_coins.amount > 0, "MUST: taker_free_coins > 0");

                taker_free_assets = calc_asset_quant(taker_free_coins, matched_price, asset_symbol);
                if (taker_free_assets <= maker_free_assets) {
                    matched_assets = taker_free_assets;
                    matched_coins  = taker_free_coins;
                    return;
                }
            } else {
                taker_free_assets = _taker_it->get_free_limit_quant();
                ASSERT(taker_free_assets.symbol == asset_symbol);
                CHECK(taker_free_assets.amount > 0, "MUST: taker_free_assets > 0");
            }

            matched_assets = (taker_free_assets < maker_free_assets) ? taker_free_assets : maker_free_assets;
            matched_coins = calc_coin_quant(matched_assets, matched_price, coin_symbol);
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
                    if (limit_buy_it.stored_order().order_id > limit_sell_it.stored_order().order_id) {
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

}// namespace dex