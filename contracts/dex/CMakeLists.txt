add_contract(dex dex ${CMAKE_CURRENT_SOURCE_DIR}/src/dex.cpp)

target_include_directories(dex
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(dex
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/dex.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/dex.contracts.md @ONLY )

target_compile_options( dex PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )