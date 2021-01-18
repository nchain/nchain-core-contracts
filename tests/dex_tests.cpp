
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

        create_accounts( { N(dex.admin), N(dex.matcher), N(dex.payee),
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

    fc::variant get_symbol_pair( uint64_t sym_pair_id)
    {
        vector<char> data = get_row_by_account( N(dex), N(dex), N(sympair), name(sym_pair_id) );
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "symbol_pair_t", data, abi_serializer_max_time );
    }

    fc::variant get_order( uint64_t order_id)
    {
        vector<char> data = get_row_by_account( N(dex), N(dex), N(order), name(order_id) );
        std::cout << "data empty=" << data.empty() << std::endl;
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "order_t", data, abi_serializer_max_time );
    }

    action_result setconfig( const variant_object &conf ) {
        return push_action( N(dex), N(setconfig), mvo()
            ( "conf", conf)
        );
    }

    action_result setsympair(const extended_symbol &asset_symbol,
                             const extended_symbol &coin_symbol, const asset &min_asset_quant,
                             const asset &min_coin_quant, bool enabled) {
        auto ret = push_action( N(dex.admin), N(setsympair), mvo()
            ( "asset_symbol", asset_symbol)
            ( "coin_symbol", coin_symbol)
            ( "min_asset_quant", min_asset_quant)
            ( "min_coin_quant", min_coin_quant)
            ( "enabled", enabled)
        );
        // sym_pair_id().next();
        return ret;
    }

    action_result match(uint32_t max_count, const std::vector<uint64_t> &sym_pairs) {
        return push_action( N(dex.matcher), N(match), mvo()
            ("matcher", N(dex.matcher))
            ("max_count", max_count)
            ("sym_pairs", sym_pairs)
        );
    }

    action_result cancel(const name &owner, const uint64_t &order_id) {
        return push_action( owner, N(cancel), mvo()
            ( "order_id", order_id)
        );
    }

    void init_config() {
        auto conf = mvo()
            ("admin", N(dex.admin))
            ("payee", N(dex.payee))
            ("taker_ratio", 8)
            ("maker_ratio", 4)
            ("max_match_count", uint32_t(0))
            ("check_order_auth", false);

        EXECUTE_ACTION(setconfig( conf ));
        produce_blocks(1);
        auto conf_store = get_conf();
        REQUIRE_MATCHING_OBJECT(get_conf(), conf);
    }

    void init_sym_pair() {
        // add symbol pair for trading
        EXECUTE_ACTION(setsympair(BTC_SYMBOL, USD_SYMBOL, ASSET("0.00001000 BTC"), ASSET("0.1000 USD"), true));
        produce_blocks(1);
        uint64_t sym_pair_id = 1;
        auto sym_pair = get_symbol_pair(sym_pair_id);

        REQUIRE_MATCH_OBJ( sym_pair,
            MATCH_FIELD("sym_pair_id", sym_pair_id)
            MATCH_FIELD_OBJ("asset_symbol", BTC_SYMBOL)
            MATCH_FIELD_OBJ("coin_symbol", USD_SYMBOL)
            MATCH_FIELD("min_asset_quant", "0.00001000 BTC")
            MATCH_FIELD("min_coin_quant", "0.1000 USD")
            MATCH_FIELD("enabled", "1")
        );
    }

    mvo init_buy_order(uint64_t sym_pair_id) {
        // buy order
        // order:<type>:<side>:<sym_pair_id>:<limit_quantity>:<price>:<external_id>[:<taker_ratio>:[maker_ratio]]

        string buy_memo = fc::format_string("order:${sym_pair_id}:limit:buy:0.01000000 BTC:1000000000000:1",
                                      mvo()
                                      ("sym_pair_id", sym_pair_id));
        EXECUTE_ACTION(eosio_token.transfer(N(alice), N(dex), ASSET("100.0000 USD"), buy_memo));
        uint64_t order_id = 1;
        auto buy_order = get_order(order_id);
        auto expected_order = mvo()
            ("sym_pair_id", 1)
            ("order_id", order_id)
            ("owner", "alice")
            ("order_type", "limit")
            ("order_side", "buy")
            ("asset_quant", "0.01000000 BTC")
            ("coin_quant", "100.0000 USD")
            ("price", "1000000000000")
            ("external_id", 1)
            ("taker_ratio", 8)
            ("maker_ratio", 4)
            ("create_time", get_head_block_time())
            ("matched_assets", "0")
            ("matched_coins", "0")
            ("is_complete", "0");
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
    buy_order("is_complete", "1");
    REQUIRE_MATCHING_OBJECT( new_buy_order, buy_order );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( dex_match_test, dex_tester ) try {

    init_config();
    init_sym_pair();

    // buy order
        // order:<type>:<side>:<sym_pair_id>:<limit_quantity>:<price>:<external_id>[:<taker_ratio>:[maker_ratio]]

    string buy_memo = "order:1:limit:buy:0.01000000 BTC:1000000000000:1";
    EXECUTE_ACTION(eosio_token.transfer(N(alice), N(dex), ASSET("100.0000 USD"), buy_memo));
    uint64_t buy_order_id = 1;
    auto buy_order = mvo()
        ("sym_pair_id", 1)
        ("order_id", buy_order_id)
        ("owner", "alice")
        ("order_type", "limit")
        ("order_side", "buy")
        ("asset_quant", "0.01000000 BTC")
        ("coin_quant", "100.0000 USD")
        ("price", "1000000000000")
        ("external_id", 1)
        ("taker_ratio", 8)
        ("maker_ratio", 4)
        ("create_time", get_head_block_time())
        ("matched_assets", "0")
        ("matched_coins", "0")
        ("is_complete", "0");

    auto new_buy_order = get_order(buy_order_id);
    REQUIRE_MATCHING_OBJECT( new_buy_order, buy_order );

    // sell order
    //order  order:<type>:<side>:<asset_quant>:<coin_quant>:<price>:<ex_id>
    string sell_memo = "order:1:limit:sell:0.01000000 BTC:1000000000000:2";
    EXECUTE_ACTION(eosio_token.transfer(N(bob), N(dex), ASSET("0.01000000 BTC"), sell_memo));
    uint64_t sell_order_id = 2;
    auto sell_order = mvo()
        ("sym_pair_id", 1)
        ("order_id", sell_order_id)
        ("owner", "bob")
        ("order_type", "limit")
        ("order_side", "sell")
        ("asset_quant", "0.01000000 BTC")
        ("coin_quant", "0.0000 USD")
        ("price", "1000000000000")
        ("external_id", 2)
        ("taker_ratio", 8)
        ("maker_ratio", 4)
        ("create_time", get_head_block_time())
        ("matched_assets", "0")
        ("matched_coins", "0")
        ("is_complete", "0");

    auto new_sell_order = get_order(sell_order_id);
    REQUIRE_MATCHING_OBJECT( new_sell_order, sell_order);

    // match
    EXECUTE_ACTION(match(100, {})); // empty sym_pairs, match all sym_pairs

    buy_order
        ("matched_assets", "1000000")
        ("matched_coins", "1000000")
        ("is_complete", 1);
    auto matched_buy_order = get_order(buy_order_id);
    BOOST_CHECK(!matched_buy_order.is_null());
    REQUIRE_MATCHING_OBJECT( matched_buy_order, buy_order );

    sell_order
            ("matched_assets", "1000000")
            ("matched_coins", "1000000")
            ("is_complete", 1);
    auto matched_sell_order = get_order(sell_order_id);
    REQUIRE_MATCHING_OBJECT( matched_sell_order, sell_order );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
