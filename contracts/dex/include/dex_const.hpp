#pragma once

#include <cstdint>
#include <eosio/name.hpp>

constexpr int64_t PRICE_PRECISION           = 100000000; // 10^8, the price precision

constexpr int64_t RATIO_PRECISION           = 10000;     // 10^4, the ratio precision
constexpr int64_t FEE_RATIO_MAX             = 4999;      // 49.99%, max fee ratio
constexpr int64_t DEX_MAKER_FEE_RATIO       = 4;         // 0.04%, dex maker fee ratio
constexpr int64_t DEX_TAKER_FEE_RATIO       = 8;         // 0.04%, dex taker fee ratio

constexpr uint32_t DEX_MATCH_COUNT_MAX      = 50;         // the max dex match count.

constexpr uint64_t OLD_DATA_OUTDATE_DAYS   = 90;

constexpr int64_t MEMO_LEN_MAX              = 255;        // 0.001%, max memo length
constexpr int64_t URL_LEN_MAX               = 255;        // 0.001%, max url length

#define DEX_CONTRACT_PROP eosio::contract("dex")
#define DEX_CONTRACT [[DEX_CONTRACT_PROP]]
#define DEX_TABLE [[eosio::table, DEX_CONTRACT_PROP]]
#define DEX_TABLE_NAME(name) [[eosio::table(name), DEX_CONTRACT_PROP]]

