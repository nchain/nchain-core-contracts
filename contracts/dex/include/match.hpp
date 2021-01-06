#pragma once

#include "table.hpp"
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
        CHECK(digit <= 18, "precision digit " + std::to_string(digit) + " should be <= 18");
        return power10(digit);
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

    template<typename match_index_t>
    class matching_order_iterator {
    public:
        enum status_t {
            CLOSED,
            OPENED,
            MATCHING,
            COMPLETE
        };
    public:
        matching_order_iterator(match_index_t &match_index, uint64_t sym_pair_id, order_side_t side,
                                order_type_t type)
            : _match_index(match_index), _it(match_index.end()), _sym_pair_id(sym_pair_id),
              _order_side(side), _order_type(type) {

            print("creating matching order itr! sym_pair_id=", _sym_pair_id, ", side=", _order_side, ", type=", _order_type, "\n");
            if (_order_side == order_side::BUY) {
                _key = make_order_match_idx(_sym_pair_id, false, _order_side, _order_type,
                                           std::numeric_limits<uint64_t>::max(), 0);
            } else { // _order_side == order_side::SELL
                _key = make_order_match_idx(_sym_pair_id, false, _order_side, _order_type, 0, 0);
            }
            _it       = _match_index.upper_bound(_key);
            process_data();
        };

        template<typename table_t>
        void complete_and_next(table_t &table) {
            ASSERT(is_complete());
            const auto &store_order = *_it;
            _it++;
            table.modify(store_order, same_payer, [&]( auto& a ) {
                a.matched_assets = _matched_assets;
                a.matched_coins = _matched_coins;
                a.is_complete = true;
            });
            process_data();
        }

        template<typename table_t>
        void save_matching_order(table_t &table) {
            if (is_matching()) {
                table.modify(*_it, same_payer, [&]( auto& a ) {
                    a.matched_assets = _matched_assets;
                    a.matched_coins = _matched_coins;
                });
            }
        }

        inline const order_t &stored_order() {
            ASSERT(is_valid());
            return *_it;
        }

        inline void match(const asset &new_matched_assets, const asset &new_matched_coins) {
            ASSERT(_status == OPENED || _status == MATCHING);
            if (_status == OPENED) {
                _status = MATCHING;
            }
            bool complete = false;

            _matched_assets += new_matched_assets.amount;
            _matched_coins += new_matched_coins.amount;
            const auto &order = *_it;
            if (order.order_side == order_side::BUY) {
                CHECK(_matched_coins <= order.coin_quant.amount,
                        "The matched coins=" + std::to_string(_matched_coins) +
                        " is overflow for buy coins=" + std::to_string(order.coin_quant.amount));
                if (order.order_type == order_type::MARKET) {
                    complete = _matched_coins == order.coin_quant.amount;
                } else {
                    CHECK(_matched_assets <= order.asset_quant.amount,
                        "The matched assets=" + std::to_string(_matched_assets) +
                        " is overflow for limit buy assets=" + std::to_string(order.asset_quant.amount));
                    complete = _matched_assets == order.asset_quant.amount;
                }
            } else {
                CHECK(_matched_assets <= order.asset_quant.amount,
                    "The matched assets=" + std::to_string(_matched_assets) +
                    " is overflow for sell assets=" + std::to_string(order.asset_quant.amount));
                complete = _matched_assets == order.asset_quant.amount;
            }

            if (complete) {
                _status = COMPLETE;
                if (order.order_side == order_side::BUY && order.order_type == order_type::LIMIT) {
                    assert(order.coin_quant.amount >= _matched_coins);
                    if (order.coin_quant.amount > _matched_coins) {
                        _refund_coins = order.coin_quant.amount - _matched_coins;
                    }
                }
            }
        }

        inline bool is_valid() const {
            return _status != CLOSED;
        }

        inline bool is_complete() const {
            return _status == COMPLETE;
        }

        inline bool is_matching() const {
            return _status == MATCHING;
        }

        inline int64_t get_free_assets() const {
            ASSERT(is_valid() && _it->asset_quant.amount >= _matched_assets);
            return _it->asset_quant.amount - _matched_assets;
        }

        inline int64_t get_free_coins() const {
            ASSERT(is_valid() && _it->coin_quant.amount >= _matched_coins);
            return _it->coin_quant.amount - _matched_coins;
        }

        inline int64_t get_refund_coins() const {
            ASSERT(is_complete());
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
            _refund_coins = 0;
            if (_it == _match_index.end()) {
                print("matching order itr end! sym_pair_id=", _sym_pair_id, ", side=", _order_side, ", type=", _order_type, "\n");
                return;
            }

            const auto &stored_order = *_it;
            CHECK(_key < stored_order.get_order_match_idx(), "the start key must < found order key");
            // print("start key=", key, ", found key=", stored_order.get_order_match_idx(), "\n");

            if (stored_order.sym_pair_id != _sym_pair_id || stored_order.is_complete ||
                stored_order.order_side != _order_side || stored_order.order_type != _order_type) {
                return;
            }
            print("found order! order=", stored_order, "\n");
            _matched_assets = stored_order.matched_assets;
            _matched_coins  = stored_order.matched_coins;
            _status = OPENED;
        }

        match_index_t &_match_index;
        order_match_idx_key _key;
        typename match_index_t::const_iterator _it;
        uint64_t _sym_pair_id;
        order_type_t _order_type;
        order_side_t _order_side;
        status_t _status = CLOSED;

        int64_t _matched_assets = 0;      //!< total matched asset amount
        int64_t _matched_coins  = 0;      //!< total matched coin amount
        uint64_t _refund_coins = 0;

    };


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

        void calc_matched_amounts(asset &matched_assets, asset &matched_coins) {
            ASSERT(_maker_it->order_type() == order_type::LIMIT && _maker_it->stored_order().price > 0);
            matched_assets.symbol = _sym_pair.asset_symbol;
            matched_coins.symbol = _sym_pair.coin_symbol;
            uint64_t matched_price = _maker_it->stored_order().price;
            int64_t taker_free_assets = 0;
            if (_taker_it->order_side() == order_side::BUY && _taker_it->order_type() == order_type::MARKET) {
                auto taker_free_coins = _taker_it->get_free_coins();
                CHECK(taker_free_coins > 0, "MUST: taker_free_coins > 0");
                taker_free_assets = calc_asset_amount(asset(taker_free_coins, _sym_pair.coin_symbol), matched_price, _sym_pair.asset_symbol);
                if (taker_free_assets == 0) { // dust amount
                    CHECK(_maker_it->get_free_assets() > 0, "The maker order free_assets is 0");
                    matched_assets.amount = 1;
                    matched_coins.amount = taker_free_coins;
                    return;
                }
            } else {
                taker_free_assets = _taker_it->get_free_assets();
                CHECK(taker_free_assets > 0, "MUST: taker_free_assets > 0");
            }

            int64_t maker_free_assets = _maker_it->get_free_assets();
            CHECK(maker_free_assets > 0, "MUST: maker_free_assets > 0");
            matched_assets.amount = std::min(taker_free_assets, maker_free_assets);
            matched_coins.amount = calc_coin_amount(matched_assets, matched_price, _sym_pair.coin_symbol);
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

}// namespace dex