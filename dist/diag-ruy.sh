#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cmake ninja-build build-essential >/dev/null 2>&1
cd /src/librime/plugins/librime-ai-predict/deps/CTranslate2
rm -rf build
echo "=== configure WITH_RUY=ON ==="
cmake . -Bbuild -DWITH_RUY=ON -DWITH_MKL=OFF -DWITH_OPENBLAS=OFF -DWITH_CUDA=OFF \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_CLI=OFF -DBUILD_TESTS=OFF -DWITH_ACCELERATE=OFF \
  -DOPENMP_RUNTIME=NONE -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_INSTALL_PREFIX=/src/librime/plugins/librime-ai-predict 2>&1 | tail -30
