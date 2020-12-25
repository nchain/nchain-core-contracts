#pragma once

#include <map>
#include <eosio/eosio.hpp>
#include <eosio/name.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>
#include "dex_const.hpp"

namespace dex {

    using namespace eosio;

    typedef name order_type_t;
    typedef name order_side_t;

    namespace order_type {
        static const order_type_t NONE = order_type_t();
        static const order_type_t LIMIT = "limit"_n;
        static const order_type_t MARKET = "market"_n;
        // order_type_t -> index
        static const std::map<order_type_t, uint8_t> ENUM_MAP = {
            {LIMIT, 1},
            {MARKET, 2}
        };

        inline bool is_valid(const order_type_t &value) {
            return ENUM_MAP.count(value);
        }

        inline uint8_t index(const order_type_t &value) {
                if (value == NONE) return 0;
                auto it = ENUM_MAP.find(value);
                check(it != ENUM_MAP.end(), "Invalid order_type=" + value.to_string());
                return it->second;
        }
    }

    namespace order_side {
        static const order_type_t NONE = order_type_t();
        static const order_side_t BUY = "buy"_n;
        static const order_side_t SELL = "sell"_n;
        // name -> index
        static const std::map<order_side_t, uint8_t> ENUM_MAP = {
            {BUY, 1},
            {SELL, 2}
        };
        inline bool is_valid(const order_side_t &value) {
            return ENUM_MAP.count(value);
        }
        inline uint8_t index(const order_side_t &value) {
                if (value == NONE) return 0;
                auto it = ENUM_MAP.find(value);
                check(it != ENUM_MAP.end(), "Invalid order_type=" + value.to_string());
                return it->second;
        }
    }


    struct DEX_TABLE config {
        name admin;   // admin of this contract
        name settler; // settler
        name payee;   // payee of this contract
        name bank;    // bank
        int64_t maker_ratio;
        int64_t taker_ratio;
    };

    typedef eosio::singleton< "config"_n, config > config_table;

    struct DEX_TABLE global {
        uint64_t order_id    = 0; // the auto-increament id of order
        uint64_t sym_pair_id = 0; // settler
    };

    typedef eosio::singleton< "global"_n, global > global_table;

    struct global_state: public global {
    public:
        bool changed = false;

        using ptr_t = std::unique_ptr<global_state>;

        static ptr_t make_global(const name &contract) {
            std::shared_ptr<global_table> global_tbl;
            auto ret = std::make_unique<global_state>();
            ret->_global_tbl = std::make_unique<global_table>(contract, contract.value);

            if (ret->_global_tbl->exists()) {
                static_cast<global&>(*ret) = ret->_global_tbl->get();
            }
            return ret;
        }

        inline void change() {
            changed = true;
        }

        inline void save(const name &payer) {
            if (changed) {
                auto &g = static_cast<global&>(*this);
                _global_tbl->set(g, payer);
                changed = false;
            }
        }
    private:
        std::unique_ptr<global_table> _global_tbl;
    };

    inline uint64_t new_auto_inc_id(uint64_t &id) {
        if (id == 0 || id == std::numeric_limits<uint64_t>::max()) {
            id = 1;
        } else {
            id++;
        }
        return id;
    }

    inline uint64_t new_order_id(global_state &g) {
        g.change();
        return new_auto_inc_id(g.order_id);
    }

    inline uint64_t new_sym_pair_id(global_state &g) {
        g.change();
        return new_auto_inc_id(g.sym_pair_id);
    }

    static inline uint128_t make_symbols_idx(const symbol &asset_symbol, const symbol &coin_symbol) {
        return uint128_t(asset_symbol.raw()) << 64 | uint128_t(coin_symbol.raw());
    }

    static inline uint128_t revert_symbols_idx(const symbol &asset_symbol, const symbol &coin_symbol) {
        return uint128_t(coin_symbol.raw()) << 64 | uint128_t(asset_symbol.raw());
    }

    struct DEX_TABLE symbol_pair_t {
        uint64_t sym_pair_id; // auto-increment
        symbol asset_symbol;
        symbol coin_symbol;
        asset min_asset_quant;
        asset min_coin_quant;
        bool enabled;

        uint128_t primary_key() const { return sym_pair_id; }
        uint128_t get_symbols_idx() const { return make_symbols_idx(asset_symbol, coin_symbol); }
    };

    using symbols_idx = indexed_by<"symbolsidx"_n, const_mem_fun<symbol_pair_t, uint128_t,
           &symbol_pair_t::get_symbols_idx>>;

    typedef eosio::multi_index<"sympair"_n, symbol_pair_t, symbols_idx> symbol_pair_table;

    inline static symbol_pair_table make_symbol_pair_table(const name &self) {
        return symbol_pair_table(self, self.value/*scope*/);
    }

    using order_match_idx_key = fixed_bytes<32>;
    inline static order_match_idx_key make_order_match_idx(uint64_t sym_pair_id, const order_side_t &side, const order_type_t &type, uint64_t price, uint64_t order_id) {
        uint64_t option = uint64_t(order_side::index(side)) << 56 | uint64_t(order_type::index(type)) << 48;
        uint64_t price_factor = (side == order_side::BUY) ? std::numeric_limits<uint64_t>::max() - price : price;
        return order_match_idx_key::make_from_word_sequence<uint64_t>(sym_pair_id, option, price_factor, order_id);
    }

    struct DEX_TABLE order_t {
        uint64_t sym_pair_id; // id of symbol_pair_table
        uint64_t order_id; // auto-increment
        name owner;
        order_type_t order_type;
        order_side_t order_side;
        asset asset_quant;
        asset coin_quant;
        int64_t price;

        int64_t deal_asset_amount = 0;      //!< total deal asset amount
        int64_t deal_coin_amount  = 0;      //!< total deal coin amount
        // TODO: should del the order when finish??
        bool is_complete            = false;  //!< order is finish
        uint64_t primary_key() const { return order_id; }
        order_match_idx_key get_order_match_idx() const {
            return make_order_match_idx(sym_pair_id, order_side, order_type, price, order_id);
        }
    };

    using order_match_idx = indexed_by<"ordermatch"_n, const_mem_fun<order_t, order_match_idx_key,
           &order_t::get_order_match_idx>>;

    typedef eosio::multi_index<"order"_n, order_t, order_match_idx> order_table;

    inline static order_table make_order_table(const name &self) {
        return order_table(self, self.value/*scope*/);
    }


    template<typename match_index_t>
    class matching_order_iterator {
    public:
        bool is_matching = false;
    public:
        matching_order_iterator(match_index_t &match_index, uint64_t sym_pair_id, order_type_t type,
                    order_side_t side)
            : _match_index(match_index), _it(match_index.end()), _sym_pair_id(sym_pair_id),
            _order_type(type), _order_side(side) {}

        void first() {
            order_match_idx_key key;
            if (_order_side == order_side::BUY) {
                key = make_order_match_idx(_sym_pair_id, _order_side, _order_type, std::numeric_limits<uint64_t>::max(), 0);
            } else { // _order_side == order_side::SELL
                key = make_order_match_idx(_sym_pair_id, _order_side, _order_type, 0, 0);
            }
            _it = _match_index.upper_bound(key);
            _is_valid = process_data();
        };

        void next() {
            _it++;
            _is_valid = process_data();
        }

        bool is_valid() const {
            return _is_valid;
        }

        inline const order_t &stored_order() {
            assert(_is_valid);
            return *_it;
        }

        inline order_t& begin_match() {
            is_matching = true;
            return _matching_order;
        }

        inline order_t& matching_order() {
            return _matching_order;
        }

    private:
        bool process_data() {
            is_matching = false;
            if (_it == _match_index.end()) return false;

            const auto &stored_order = *_it;
            if (stored_order.sym_pair_id != _sym_pair_id || stored_order.order_side != _order_side || stored_order.order_type != _order_type ) {
                return false;
            }
            return true;
        }

        match_index_t &_match_index;
        typename match_index_t::const_iterator _it;
        uint64_t _sym_pair_id;
        order_type_t _order_type;
        order_side_t _order_side;
        bool _is_valid = false;

        order_t _matching_order;
    };

}// namespace dex