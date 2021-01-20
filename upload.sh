# host=jw
host=sh-test

rsync -rav -e ssh \
    --include='*.abi' \
    --include='*.wasm' \
    --exclude='**/CMakeFiles' \
    --exclude='*.cmake' \
    --exclude='Makefile' \
    --exclude='*.md' \
    --exclude='CMakeCache.txt' \
    --exclude='CMakeLists.txt' \
    ./build/contracts/ ${host}:/opt/mgp/wallet/contracts

rsync -rav -e ssh \
    ./unittest.sh ${host}:/opt/mgp/wallet/