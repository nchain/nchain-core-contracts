
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


static const symbol BTC_SYMBOL = symbol(8, "BTC");
static const symbol USD_SYMBOL = symbol(4, "USD");

#define ASSET(s) asset::from_string(s)

#define EXECUTE_ACTION(action_expr) BOOST_REQUIRE_EQUAL(action_expr, "")

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

        create_accounts( { N(dex.admin), N(dex.settler), N(dex.payee),
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

    action_result setsympair( const symbol &asset_symbol, const symbol &coin_symbol, const asset &min_asset_quant, const asset &min_coin_quant, bool enabled ) {
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

    action_result match() {
        return push_action( N(dex.settler), N(match), mvo()
        );
    }

    // action_result settle(const uint64_t &buy_id, const uint64_t &sell_id,
    //                         const asset &asset_quant, const asset &coin_quant,
    //                         const int64_t &price, const string &memo) {
    //     return push_action( N(dex.settler), N(settle), mvo()
    //         ( "buy_id", buy_id)
    //         ( "sell_id", sell_id)
    //         ( "asset_quant", asset_quant)
    //         ( "coin_quant", coin_quant)
    //         ( "price", price)
    //         ( "memo", memo)
    //     );
    // }
    action_result cancel(const uint64_t &order_id) {
        return push_action( N(dex), N(cancel), mvo()
            ( "order_id", order_id)
        );
    }

    abi_serializer abi_ser;
    eosio_token_helper eosio_token;
};

BOOST_AUTO_TEST_SUITE(dex_tests)


BOOST_FIXTURE_TEST_CASE( dex_settle_test, dex_tester ) try {

    auto conf = mvo()
        ("admin", N(dex.admin))
        ("settler", N(dex.settler))
        ("payee", N(dex.payee))
        ("bank", N(eosio.token))
        ("maker_ratio", int64_t(4))
        ("taker_ratio", int64_t(8));

    EXECUTE_ACTION(setconfig( conf ));
    produce_blocks(1);
    auto conf_store = get_conf();
    REQUIRE_MATCHING_OBJECT(get_conf(), conf);

    // add symbol pair for trading
    EXECUTE_ACTION(setsympair(BTC_SYMBOL, USD_SYMBOL, ASSET("0.00001000 BTC"), ASSET("0.1000 USD"), true));
    produce_blocks(1);
    uint64_t sym_pair_id = 1;
    auto sym_pair = get_symbol_pair(sym_pair_id);
    REQUIRE_MATCHING_OBJECT( sym_pair, mvo()
        ("sym_pair_id", sym_pair_id)
        ("asset_symbol", BTC_SYMBOL)
        ("coin_symbol", USD_SYMBOL)
        ("min_asset_quant", "0.00001000 BTC")
        ("min_coin_quant", "0.1000 USD")
        ("enabled", "1")
    );

    // buy order
    //order  order:<type>:<side>:<asset_quant>:<coin_quant>:<price>:<ex_id>
    string buy_memo = "order:limit:buy:0.01000000 BTC:100.0000 USD:1000000000000:1";
    EXECUTE_ACTION(eosio_token.transfer(N(alice), N(dex), ASSET("100.0000 USD"), buy_memo));
    uint64_t order_id = 1;
    auto buy_order = get_order(order_id);
    REQUIRE_MATCHING_OBJECT( buy_order, mvo()
        ("sym_pair_id", sym_pair_id)
        ("order_id", order_id)
        ("owner", "alice")
        ("order_type", "limit")
        ("order_side", "buy")
        ("asset_quant", "0.01000000 BTC")
        ("coin_quant", "100.0000 USD")
        ("price", "1000000000000")
        ("external_id", 1)
        ("deal_asset_amount", "0")
        ("deal_coin_amount", "0")
        ("is_complete", "0")
    );

    // sell order
    //order  order:<type>:<side>:<asset_quant>:<coin_quant>:<price>:<ex_id>
    string sell_memo = "order:limit:sell:0.01000000 BTC:0.0000 USD:1000000000000:2";
    EXECUTE_ACTION(eosio_token.transfer(N(bob), N(dex), ASSET("0.01000000 BTC"), sell_memo));
    order_id++;
    auto sell_order = get_order(order_id);
    REQUIRE_MATCHING_OBJECT( sell_order, mvo()
        ("sym_pair_id", sym_pair_id)
        ("order_id", order_id)
        ("owner", "bob")
        ("order_type", "limit")
        ("order_side", "sell")
        ("asset_quant", "0.01000000 BTC")
        ("coin_quant", "0.0000 USD")
        ("price", "1000000000000")
        ("external_id", 2)
        ("deal_asset_amount", "0")
        ("deal_coin_amount", "0")
        ("is_complete", "0")
    );

    // // settle
    // EXECUTE_ACTION(settle(0, 1, ASSET("0.01000000 BTC"), ASSET("100.0000 USD"), 1000000000000, ""));

    // match
    EXECUTE_ACTION(match());
    produce_blocks(1);

    auto matched_buy_order = get_order(1);
    BOOST_CHECK( matched_buy_order.is_null());
    auto matched_sell_order = get_order(2);
    BOOST_CHECK( matched_sell_order.is_null());

    // auto deal_buy_order = get_order(0);
    // REQUIRE_MATCHING_OBJECT( deal_buy_order, mvo()
    //     ("sym_pair_id", sym_pair_id().id)
    //     ("order_id", "0")
    //     ("owner", "alice")
    //     ("order_type", "limit")
    //     ("order_side", "buy")
    //     ("asset_quant", "0.01000000 BTC")
    //     ("coin_quant", "100.0000 USD")
    //     ("price", "1000000000000")
    //     ("deal_asset_amount", "1000000")
    //     ("deal_coin_amount", "1000000")
    //     ("is_finish", "1")
    // );

    // auto deal_sell_order = get_order(1);
    // REQUIRE_MATCHING_OBJECT( deal_sell_order, mvo()
    //     ("sym_pair_id", sym_pair_id().id)
    //     ("order_id", "1")
    //     ("owner", "bob")
    //     ("order_type", "limit")
    //     ("order_side", "sell")
    //     ("asset_quant", "0.01000000 BTC")
    //     ("coin_quant", "0.0000 USD")
    //     ("price", "1000000000000")
    //     ("deal_asset_amount", "1000000")
    //     ("deal_coin_amount", "1000000")
    //     ("is_finish", "1")
    // );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
