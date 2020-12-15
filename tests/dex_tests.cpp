
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

        create_accounts( { N(dex.owner), N(dex.settler), N(dex.payee),
            N(ex1.owner), N(ex1.settler), N(ex1.payee),
            N(alice), N(bob), N(carol), N(dex) } );
        produce_blocks( 2 );

        set_code( N(dex), contracts::dex_wasm() );
        set_abi( N(dex), contracts::dex_abi().data() );

        produce_blocks();

        const auto& accnt = control->db().get<account_object,by_name>( N(dex) );
        abi_def abi;
        BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
        abi_ser.set_abi(abi, abi_serializer_max_time);

        EXECUTE_ACTION(eosio_token.create(N(alice), asset::from_string("100000.0000 AAA")));
        produce_blocks(1);
        EXECUTE_ACTION(eosio_token.issue( N(alice), asset::from_string("100000.0000 AAA"), "" ));

        EXECUTE_ACTION(eosio_token.create(N(dex.owner), asset::from_string("100000.0000 USD")));
        produce_blocks(1);
        EXECUTE_ACTION(eosio_token.issue( N(dex.owner), asset::from_string("100000.0000 USD"), "" ));
        EXECUTE_ACTION(eosio_token.transfer( N(dex.owner), N(ex1.owner), asset::from_string("100.0000 USD"), "" ) );
        EXECUTE_ACTION(eosio_token.transfer( N(dex.owner), N(alice), asset::from_string("10000.0000 USD"), "" ) );

        EXECUTE_ACTION(eosio_token.create(N(dex.owner), asset::from_string("10.00000000 BTC")));
        EXECUTE_ACTION(eosio_token.issue( N(dex.owner), asset::from_string("10.00000000 BTC"), "" ));
        EXECUTE_ACTION(eosio_token.transfer( N(dex.owner), N(bob), asset::from_string("1.00000000 BTC"), "" ) );

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

    fc::variant get_config( )
    {
        auto data = get_row_by_account( N(dex), N(dex), N(config), N(config) );
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "config_t", data, abi_serializer_max_time );
    }

    fc::variant get_symbol_pair( uint64_t sym_pair_id)
    {
        vector<char> data = get_row_by_account( N(dex), N(dex), N(sympair), name(sym_pair_id) );
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "symbol_pair_t", data, abi_serializer_max_time );
    }

    fc::variant get_exchange( name ex_id)
    {
        vector<char> data = get_row_by_account( N(dex), N(dex), N(exchange), ex_id );
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "exchange_t", data, abi_serializer_max_time );
    }

    fc::variant get_order( uint64_t order_id)
    {
        vector<char> data = get_row_by_account( N(dex), N(dex), N(order), name(order_id) );
        return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "order_t", data, abi_serializer_max_time );
    }

    action_result dex_init( const name &owner, const name &settler, const name &payee ) {
        return push_action( N(dex), N(init), mvo()
            ( "owner", owner)
            ( "settler", settler)
            ( "payee", payee)
        );
    }

    struct auto_inc_id {
        uint64_t id = 0;
        uint64_t next_id = 0;

        void next() {
            if (next_id == 0) {
                // first value
                id = 0;
                next_id = 1;
            } else {
                id = next_id;
                next_id++;
            }
        }
    };

    auto_inc_id& sym_pair_id() {
        static auto_inc_id id;
        return id;
    }


    action_result addsympair( const symbol &asset_symbol, const symbol &coin_symbol ) {
        auto ret = push_action( N(dex), N(addsympair), mvo()
            ( "asset_symbol", asset_symbol)
            ( "coin_symbol", coin_symbol)
        );
        sym_pair_id().next();
        return ret;
    }

    action_result settle(const uint64_t &buy_id, const uint64_t &sell_id,
                            const asset &asset_quant, const asset &coin_quant,
                            const int64_t &price, const string &memo) {
        return push_action( N(dex), N(settle), mvo()
            ( "buy_id", buy_id)
            ( "sell_id", sell_id)
            ( "asset_quant", asset_quant)
            ( "coin_quant", coin_quant)
            ( "price", price)
            ( "memo", memo)
        );
    }
    action_result cancel(const uint64_t &order_id) {
        return push_action( N(dex), N(settle), mvo()
            ( "order_id", order_id)
        );
    }

    abi_serializer abi_ser;
    eosio_token_helper eosio_token;
};

BOOST_AUTO_TEST_SUITE(dex_tests)


BOOST_FIXTURE_TEST_CASE( dex_settle_test, dex_tester ) try {

    // init contract config
    auto new_dex = dex_init( N(dex.owner), N(dex.settler), N(dex.payee));
    BOOST_REQUIRE_EQUAL(new_dex, "");
    produce_blocks(1);
    auto config = get_config();
    REQUIRE_MATCHING_OBJECT( config, mvo()
        ("owner", "dex.owner")
        ("settler", "dex.settler")
        ("payee", "dex.payee")
    );

    // add symbol pair for trading
    auto sym_pair_ret = addsympair(BTC_SYMBOL, USD_SYMBOL);
    BOOST_REQUIRE_EQUAL(sym_pair_ret, "");
    produce_blocks(1);
    auto sym_pair = get_symbol_pair(sym_pair_id().id);
    REQUIRE_MATCHING_OBJECT( sym_pair, mvo()
        ("sym_pair_id", std::to_string(sym_pair_id().id))
        ("asset_symbol", BTC_SYMBOL.to_string())
        ("coin_symbol", USD_SYMBOL.to_string())
    );

    // create exchange
    //memo  ex:<ex_id>:<owner>:<payee>:<open_mode>:<maker_ratio>:<taker_ratio>:<url>:<memo>
    string ex_memo = "ex:ex1:ex1.owner:ex1.payee:public:4:8:ex1.com:ex1 is best";
    EXECUTE_ACTION(eosio_token.transfer(N(dex.owner), N(dex), ASSET("100.0000 USD"), ex_memo));
    auto ex1 = get_exchange(N(ex1));
    REQUIRE_MATCHING_OBJECT( ex1, mvo()
        ("ex_id", "ex1")
        ("owner", "ex1.owner")
        ("payee", "ex1.payee")
        ("open_mode", "public")
        ("maker_ratio", "4")
        ("taker_ratio", "8")
        ("url", "ex1.com")
        ("memo", "ex1 is best")
    );

    // buy order
    //order  order:<type>:<side>:<asset_quant>:<coin_quant>:<price>:<ex_id>
    string buy_memo = "order:limit_price:buy:0.01000000 BTC:100.0000 USD:1000000000000:ex1";
    EXECUTE_ACTION(eosio_token.transfer(N(alice), N(dex), ASSET("100.0000 USD"), buy_memo));
    auto buy_order = get_order(0);
    REQUIRE_MATCHING_OBJECT( buy_order, mvo()
        ("order_id", "0")
        ("owner", "alice")
        ("order_type", "limit_price")
        ("order_side", "buy")
        ("asset_quant", "0.01000000 BTC")
        ("coin_quant", "100.0000 USD")
        ("price", "1000000000000")
        ("deal_asset_amount", "0")
        ("deal_coin_amount", "0")
        ("is_finish", "0")
    );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
