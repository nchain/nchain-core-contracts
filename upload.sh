#!/bin/bash

sync() {
    dest=/opt/mgp/node_wallet/data/contracts

    rsync -rav -e ssh \
        --include='*.abi' \
        --include='*.wasm' \
        --exclude='**/CMakeFiles' \
        --exclude='*.cmake' \
        --exclude='Makefile' \
        --exclude='*.md' \
        --exclude='CMakeCache.txt' \
        --exclude='CMakeLists.txt' \
        ./build/contracts/dex ${host}:${dest}/dex
}

sync jw
sync sh-test