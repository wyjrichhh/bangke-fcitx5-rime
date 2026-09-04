#!/bin/bash
set -e
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cmake ninja-build libboost-dev libboost-regex-dev libboost-filesystem-dev libboost-system-dev libboost-locale-dev libyaml-cpp-dev libgoogle-glog-dev libleveldb-dev libmarisa-dev libopencc-dev >/dev/null 2>&1
cd /src/librime
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON -DBUILD_MERGED_PLUGINS=OFF -DENABLE_EXTERNAL_PLUGINS=ON \
  -DBUILD_TEST=OFF -DBUILD_SAMPLE=OFF -DBoost_DEBUG=ON 2>&1 | grep -iE "boost|Boost" | head -40
