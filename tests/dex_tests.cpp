
#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include "eosio.system_tester.hpp"

#include "Runtime/Runtime.h"

#include <fc/variant_object.hpp>

using namespace eosio::testing;
using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;
using namespace std;

using mvo = fc::mutable_variant_object;

static const name BANK = N(eosio.token);

static const extended_symbol BTC_SYMBOL = extended_symbol{symbol(8, "BTC"), BANK};
static const extended_symbol USD_SYMBOL = extended_symbol{symbol(4, "USD"), BANK};

#define ASSET(s) asset::from_string(s)

#define EXECUTE_ACTION(action_expr) BOOST_REQUIRE_EQUAL(action_expr, "")


#define REQUIRE_MATCH_OBJ(obj, statements)                                                         \
    {                                                                                              \
        auto __o = fc::variant(obj);                                                               \
        BOOST_REQUIRE_EQUAL(true, __o.is_object());                                                \
        const auto &o = __o.get_object();                                                          \
        statements                                                                                 \
    }

#define MATCH_FIELD(field, value) BOOST_REQUIRE_EQUAL(o[field], fc::variant(value));

#define MATCH_FIELD_OBJ(field, value) REQUIRE_MATCHING_OBJECT(o[field], fc::variant(value));

#define REQUIRE_MATCH_FIELD_OBJ(field, statements) REQUIRE_MATCH_OBJ(o[field], statements);

class eosio_token_helper {
public:
    using action_result = tester::action_result;

   eosio_token_helper(tester &t): _tester(t) {
      _tester.produce_blocks( 2 );

      _tester.create_accounts( { N(eosio.token) } );
      _tester.produce_blocks( 2 );

      _tester.set_code( N(eosio.token), contracts::token_wasm() );
      _tester.set_abi( N(eosio.token), contracts::token_abi().data() );

      _tester.produce_blocks();

      const auto& accnt = _tester.control->db().get<account_object,by_name>( N(eosio.token) );
      abi_def abi;
      BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
      abi_ser.set_abi(abi, _tester.abi_serializer_max_time);
   }

   action_result push_action( const account_name& signer, const action_name &name, const variant_object &data ) {
      string action_type_name = abi_ser.get_action_type(name);

      action act;
      act.account = N(eosio.token);
      act.name    = name;
      act.data    = abi_ser.variant_to_binary( action_type_name, data, _tester.abi_serializer_max_time );

      return _tester.push_action( std::move(act), signer.to_uint64_t() );
   }

   fc::variant get_stats( const string& symbolname )
   {
      auto symb = eosio::chain::symbol::from_string(symbolname);
      auto symbol_code = symb.to_symbol_code().value;
      vector<char> data = _tester.get_row_by_account( N(eosio.token), name(symbol_code), N(stat), account_name(symbol_code) );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "currency_stats", data, _tester.abi_serializer_max_time );
   }

   fc::variant get_account( account_name acc, const string& symbolname)
   {
      auto symb = eosio::chain::symbol::from_string(symbolname);
      auto symbol_code = symb.to_symbol_code().value;
      vector<char> data = _tester.get_row_by_account( N(eosio.token), acc, N(accounts), account_name(symbol_code) );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "account", data, _tester.abi_serializer_max_time );
   }

   action_result create( account_name issuer,
                         asset        maximum_supply ) {

      return push_action( N(eosio.token), N(create), mvo()
           ( "issuer", issuer)
           ( "maximum_supply", maximum_supply)
      );
   }

   action_result issue( account_name issuer, asset quantity, string memo ) {
      return push_action( issuer, N(issue), mvo()
           ( "to", issuer)
           ( "quantity", quantity)
           ( "memo", memo)
      );
   }

   action_result retire( account_name issuer, asset quantity, string memo ) {
      return push_action( issuer, N(retire), mvo()
           ( "quantity", quantity)
           ( "memo", memo)
      );

   }

   action_result transfer( account_name from,
                  account_name to,
                  asset        quantity,
                  string       memo ) {
      return push_action( from, N(transfer), mvo()
           ( "from", from)
           ( "to", to)
           ( "quantity", quantity)
           ( "memo", memo)
      );
   }

   action_result open( account_name owner,
                       const string& symbolname,
                       account_name ram_payer    ) {
      return push_action( ram_payer, N(open), mvo()
           ( "owner", owner )
           ( "symbol", symbolname )
           ( "ram_payer", ram_payer )
      );
   }

   action_result close( account_name owner,
                        const string& symbolname ) {
      return push_action( owner, N(close), mvo()
           ( "owner", owner )
           ( "symbol", "0,CERO" )
      );
   }

   tester &_tester;
   abi_serializer abi_ser;
};

class dex_tester : public tester {
public:

    dex_tester(): eosio_token(*this) {
        produce_blocks( 2 );

        create_accounts( { N(dex.admin), N(dex.matcher), N(dex.fee),
            N(alice), N(bob), N(carol), N(dex) } );
        produce_blocks( 2 );

        set_code( N(dex), contracts::dex_wasm() );
        set_abi( N(dex), contracts::dex_abi().data() );

        produce_blocks();

        const auto& accnt = control->db().get<account_object,by_name>( N(dex) );
        abi_def abi;
        BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
        abi_ser.set_abi(abi, abi_serializer_max_time);

        EXECUTE_ACTION(eosio_token.create(N(dex.admin), asset::from_string("100000.0000 USD")));
        produce_blocks(1);
        EXECUTE_ACTION(eosio_token.issue( N(dex.admin), asset::from_string("100000.0000 USD"), "" ));
        EXECUTE_ACTION(eosio_token.transfer( N(dex.admin), N(alice), asset::from_string("10000.0000 USD"), "" ) );

        EXECUTE_ACTION(eosio_token.create(N(dex.admin), asset::from_string("10.00000000 BTC")));
        EXECUTE_ACTION(eosio_token.issue( N(dex.admin), asset::from_string("10.00000000 BTC"), "" ));
        EXECUTE_ACTION(eosio_token.transfer( N(dex.admin), N(bob), asset::from_string("1.00000000 BTC"), "" ) );

    }

    inline time_point get_head_block_time() {
        return control->head_block_state()->block->timestamp.to_time_point();
    }

    action_result push_action( const account_name& signer, const action_name &name, const variant_object &data ) {
        string action_type_name = abi_ser.get_action_type(name);
        BOOST_REQUIRE_NE(action_type_name, "");
        action act;
        act.account = N(dex);
        act.name    = name;
        act.data    = abi_ser.variant_to_binary( action_type_name, data,abi_serializer_max_time );

        return base_tester::push_action( std::move(act), signer.to_uint64_t() );
    }

    fc::variant get_conf( )
    {
        auto data = get_row_by_account( N(dex), N(dex), N(config), N(config) );
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "config", data, abi_serializer_max_time );
    }

    fc::variant get_symbol_pair( uint64_t sympair_id)
    {
        vector<char> data = get_row_by_account( N(dex), N(dex), N(sympair), name(sympair_id) );
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "symbol_pair_t", data, abi_serializer_max_time );
    }

    fc::variant get_order( uint64_t order_id)
    {
        vector<char> data = get_row_by_account( N(dex), N(dex), N(order), name(order_id) );
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "order_t", data, abi_serializer_max_time );
    }

    fc::variant get_account( const name &user, uint64_t account_id)
    {
        vector<char> data = get_row_by_account( N(dex), user, N(account), name(account_id) );
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "account_t", data, abi_serializer_max_time );
    }

    action_result setconfig( const variant_object &conf ) {
        return push_action( N(dex), N(setconfig), mvo()
            ( "conf", conf)
        );
    }

    action_result setsympair(const extended_symbol &asset_symbol,
                             const extended_symbol &coin_symbol, const asset &min_asset_quant,
                             const asset &min_coin_quant, bool only_accept_coin_fee, bool enabled) {
        auto ret = push_action( N(dex.admin), N(setsympair), mvo()
            ( "asset_symbol", asset_symbol)
            ( "coin_symbol", coin_symbol)
            ( "min_asset_quant", min_asset_quant)
            ( "min_coin_quant", min_coin_quant)
            ( "only_accept_coin_fee", only_accept_coin_fee)
            ( "enabled", enabled)
        );
        // sympair_id().next();
        return ret;
    }

    action_result deposit(const name &from, const asset &quantity) {
        return eosio_token.transfer(from, N(dex), quantity, "deposit");
    }

    struct order_config_ex_t {
        uint64_t taker_fee_ratio = 0;
        uint64_t maker_fee_ratio = 0;
    };

    action_result neworder(const name &user, const uint64_t &sympair_id,
        const name &order_type, const name &order_side,
        const asset &limit_quant,
        const asset &frozen_quant,
        const asset &price,
        const uint64_t &external_id,
        const std::optional<order_config_ex_t> &order_config_ex) {

        return push_action( user, N(neworder), mvo()
            ( "user", user)
            ( "sympair_id", sympair_id)
            ( "order_type", order_type)
            ( "order_side", order_side)
            ( "limit_quant", limit_quant)
            ( "frozen_quant", frozen_quant)
            ( "price", price)
            ( "external_id", external_id)
            ( "order_config_ex", fc::variant())
        );
    }

    action_result match(uint32_t max_count, const std::vector<uint64_t> &sym_pairs, const string &memo) {
        return push_action( N(dex.matcher), N(match), mvo()
            ("matcher", N(dex.matcher))
            ("max_count", max_count)
            ("sym_pairs", sym_pairs)
            ("memo", memo)
        );
    }

    action_result cancel(const name &owner, const uint64_t &order_id) {
        return push_action( owner, N(cancel), mvo()
            ( "order_id", order_id)
        );
    }

    void init_config() {
        auto conf = mvo()
            ("dex_admin", N(dex.admin))
            ("dex_fee_collector", N(dex.fee))
            ("taker_fee_ratio", 8)
            ("maker_fee_ratio", 4)
            ("max_match_count", uint32_t(0))
            ("admin_sign_required", false)
            ("old_data_outdate_sec", 90 * 3600 * 24);

        EXECUTE_ACTION(setconfig( conf ));
        produce_blocks(1);
        auto conf_store = get_conf();
        REQUIRE_MATCHING_OBJECT(get_conf(), conf);
    }

    void init_sym_pair() {
        // add symbol pair for trading
        EXECUTE_ACTION(setsympair(BTC_SYMBOL, USD_SYMBOL, ASSET("0.00001000 BTC"), ASSET("0.1000 USD"), false, true));
        uint64_t sympair_id = 1;
        auto sym_pair = get_symbol_pair(sympair_id);

        REQUIRE_MATCH_OBJ( sym_pair,
            MATCH_FIELD("sympair_id", sympair_id)
            MATCH_FIELD_OBJ("asset_symbol", BTC_SYMBOL)
            MATCH_FIELD_OBJ("coin_symbol", USD_SYMBOL)
            MATCH_FIELD("min_asset_quant", "0.00001000 BTC")
            MATCH_FIELD("min_coin_quant", "0.1000 USD")
            MATCH_FIELD("only_accept_coin_fee", "0")
            MATCH_FIELD("enabled", "1")
        );
    }

    mvo init_buy_order(uint64_t sympair_id) {
        // buy order
        EXECUTE_ACTION(deposit(N(alice), ASSET("100.0000 USD")));
        auto account = get_account(N(alice), 0);

        REQUIRE_MATCH_OBJ( account,
            MATCH_FIELD("id", 0)
            REQUIRE_MATCH_FIELD_OBJ("balance",
                MATCH_FIELD("contract", "eosio.token")
                MATCH_FIELD("quantity", "100.0000 USD")
            )
        );

        uint64_t order_id = 1;
        EXECUTE_ACTION(neworder(N(alice), order_id, N(limit), N(buy), ASSET("0.01000000 BTC"), ASSET("100.0000 USD"),
                ASSET("10000.0000 USD"), 1, std::nullopt));
        auto buy_order = get_order(order_id);
        auto expected_order = mvo()
            ("sympair_id", 1)
            ("order_id", order_id)
            ("owner", "alice")
            ("order_type", "limit")
            ("order_side", "buy")
            ("price", "10000.0000 USD")
            ("limit_quant", "0.01000000 BTC")
            ("frozen_quant", "100.0000 USD")
            ("external_id", 1)
            ("taker_fee_ratio", 8)
            ("maker_fee_ratio", 4)
            ("matched_assets", "0.00000000 BTC")
            ("matched_coins", "0.0000 USD")
            ("matched_fee", "0.00000000 BTC")
            ("status", "matchable")
            ("created_at", get_head_block_time())
            ("last_updated_at", get_head_block_time());
        REQUIRE_MATCHING_OBJECT( buy_order, expected_order );
        return expected_order;
    }

    abi_serializer abi_ser;
    eosio_token_helper eosio_token;
};

BOOST_AUTO_TEST_SUITE(dex_tests)


BOOST_FIXTURE_TEST_CASE( dex_cancel_test, dex_tester ) try {

    init_config();
    init_sym_pair();
    auto buy_order = init_buy_order(1);

    EXECUTE_ACTION(cancel(N(alice), 1));
    auto new_buy_order = get_order(1);
    // update the buy_order;
    buy_order("status", "canceled")
    ("last_updated_at", get_head_block_time());
    REQUIRE_MATCHING_OBJECT( new_buy_order, buy_order );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( dex_match_test, dex_tester ) try {

    init_config();
    init_sym_pair();

    // buy order
    EXECUTE_ACTION(deposit(N(alice), ASSET("100.0000 USD")));
    auto buyer_account = get_account(N(alice), 0);
    REQUIRE_MATCH_OBJ( buyer_account,
        MATCH_FIELD("id", 0)
        REQUIRE_MATCH_FIELD_OBJ("balance",
            MATCH_FIELD("contract", "eosio.token")
            MATCH_FIELD("quantity", "100.0000 USD")
        )
    );

    uint64_t buy_order_id = 1;
    EXECUTE_ACTION(neworder(N(alice), 1, N(limit), N(buy), ASSET("0.01000000 BTC"), ASSET("100.0000 USD"),
            ASSET("10000.0000 USD"), 1, std::nullopt));
    auto buy_order = mvo()
        ("sympair_id", 1)
        ("order_id", buy_order_id)
        ("owner", "alice")
        ("order_type", "limit")
        ("order_side", "buy")
        ("price", "10000.0000 USD")
        ("limit_quant", "0.01000000 BTC")
        ("frozen_quant", "100.0000 USD")
        ("external_id", 1)
        ("taker_fee_ratio", 8)
        ("maker_fee_ratio", 4)
        ("matched_assets", "0.00000000 BTC")
        ("matched_coins", "0.0000 USD")
        ("matched_fee", "0.00000000 BTC")
        ("status", "matchable")
        ("created_at", get_head_block_time())
        ("last_updated_at", get_head_block_time());

    auto new_buy_order = get_order(buy_order_id);
    REQUIRE_MATCHING_OBJECT( new_buy_order, buy_order );

    // sell order
    EXECUTE_ACTION(deposit(N(bob), ASSET("0.01000000 BTC")));
    auto seller_account = get_account(N(bob), 0);
    REQUIRE_MATCH_OBJ( seller_account,
        MATCH_FIELD("id", 0)
        REQUIRE_MATCH_FIELD_OBJ("balance",
            MATCH_FIELD("contract", "eosio.token")
            MATCH_FIELD("quantity", "0.01000000 BTC")
        )
    );

    uint64_t sell_order_id = 2;
    EXECUTE_ACTION(neworder(N(bob), 1, N(limit), N(sell), ASSET("0.01000000 BTC"), ASSET("0.01000000 BTC"),
            ASSET("10000.0000 USD"), 2, std::nullopt));
    auto sell_order = mvo()
        ("sympair_id", 1)
        ("order_id", sell_order_id)
        ("owner", "bob")
        ("order_type", "limit")
        ("order_side", "sell")
        ("price", "10000.0000 USD")
        ("limit_quant", "0.01000000 BTC")
        ("frozen_quant", "0.01000000 BTC")
        ("external_id", 2)
        ("taker_fee_ratio", 8)
        ("maker_fee_ratio", 4)
        ("matched_assets", "0.00000000 BTC")
        ("matched_coins", "0.0000 USD")
        ("matched_fee", "0.0000 USD")
        ("status", "matchable")
        ("created_at", get_head_block_time())
        ("last_updated_at", get_head_block_time());

    auto new_sell_order = get_order(sell_order_id);
    REQUIRE_MATCHING_OBJECT( new_sell_order, sell_order);

    // match
    EXECUTE_ACTION(match(100, {}, "test")); // empty sym_pairs, match all sym_pairs

    buy_order
        ("matched_assets", "0.01000000 BTC")
        ("matched_coins", "100.0000 USD")
        ("matched_fee", "0.00000400 BTC")
        ("status", "completed")
        ("last_updated_at", get_head_block_time());
    auto matched_buy_order = get_order(buy_order_id);
    BOOST_CHECK(!matched_buy_order.is_null());
    REQUIRE_MATCHING_OBJECT( matched_buy_order, buy_order );

    sell_order
        ("matched_assets", "0.01000000 BTC")
        ("matched_coins", "100.0000 USD")
        ("matched_fee", "0.0800 USD")
        ("status", "completed")
        ("last_updated_at", get_head_block_time());
    auto matched_sell_order = get_order(sell_order_id);
    REQUIRE_MATCHING_OBJECT( matched_sell_order, sell_order );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
