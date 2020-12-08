#pragma once

#include <cstdint>
#include <eosio/name.hpp>

constexpr int64_t PRICE_PRECISION           = 100000000; // 10^8, the price precision

constexpr int64_t RATIO_PRECISION           = 10000;     // 10^4, the ratio precision
constexpr int64_t DEX_FRICTION_FEE_RATIO    = 10;        // 0.001%, the friction fee ratio

constexpr int64_t MEMO_LEN_MAX              = 255;        // 0.001%, the friction fee ratio
constexpr int64_t URL_LEN_MAX               = 255;        // 0.001%, the friction fee ratio

constexpr eosio::name BANK                  = "eosio.token"_n;