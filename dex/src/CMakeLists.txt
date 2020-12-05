project(dex)

set(EOSIO_WASM_OLD_BEHAVIOR "Off")
find_package(eosio.cdt)

add_contract( dex dex dex.cpp )
target_include_directories( dex PUBLIC ${CMAKE_SOURCE_DIR}/../include )
target_ricardian_directory( dex ${CMAKE_SOURCE_DIR}/../ricardian )