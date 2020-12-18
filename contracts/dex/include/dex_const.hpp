#pragma once

#include <cstdint>
#include <eosio/name.hpp>

constexpr int64_t PRICE_PRECISION           = 100000000; // 10^8, the price precision

constexpr int64_t RATIO_PRECISION           = 10000;     // 10^4, the ratio precision
constexpr int64_t DEX_MAKER_FEE_RATIO       = 4;         // 0.0004%, dex maker fee ratio
constexpr int64_t DEX_TAKER_FEE_RATIO       = 8;         // 0.0004%, dex taker fee ratio

constexpr int64_t MEMO_LEN_MAX              = 255;        // 0.001%, max memo length
constexpr int64_t URL_LEN_MAX               = 255;        // 0.001%, max url length
