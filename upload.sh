#!/bin/bash

sync() {
    host=$1
    dest=/opt/mgp/node_wallet/data/contracts

    rsync -rav -e ssh \
        --include='*.abi' \
        --include='*.wasm' \
        --exclude='**/CMakeFiles' \
        --exclude='*.cmake' \
        --exclude='Makefile' \
        --exclude="include" \
        --exclude="ricardian" \
        --exclude='*.md' \
        --exclude='CMakeCache.txt' \
        --exclude='CMakeLists.txt' \
        ./build/contracts/dex ${host}:${dest}/
}

sync jw
sync sh-test