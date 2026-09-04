#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cmake ninja-build build-essential pkg-config \
  libboost-dev libboost-filesystem-dev libboost-system-dev libboost-regex-dev libboost-locale-dev \
  libyaml-cpp-dev libgoogle-glog-dev libleveldb-dev libmarisa-dev libopencc-dev >/dev/null 2>&1

echo "=== 重编 CTranslate2 (WITH_RUY=ON) ==="
cd /src/librime/plugins/librime-ai-predict
rm -rf deps/CTranslate2/build
make -f deps.mk ctranslate2 2>&1 | tail -3

echo "=== 重编 librime + 插件 ==="
cd /src/librime
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=ON -DBUILD_MERGED_PLUGINS=OFF -DENABLE_EXTERNAL_PLUGINS=ON \
  -DBUILD_TEST=OFF -DBUILD_SAMPLE=OFF >/dev/null 2>&1
ninja -C build -j "$(nproc)" 2>&1 | tail -3

echo "=== 产物 ==="
ls -la /src/librime/build/lib/rime-plugins/librime-ai-predict.so /src/librime/build/lib/librime.so.1.16.1
echo "=== 确认插件链接了 Ruy (应无 No SGEMM 问题的后端) ==="
nm -C /src/librime/build/lib/rime-plugins/librime-ai-predict.so 2>/dev/null | grep -iE "ruy|sgemm" | head -3
