#!/bin/bash
# 在 ubuntu:22.04 容器内运行，把已构建产物 DESTDIR 进 stage
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cmake ninja-build pkg-config \
  extra-cmake-modules gettext libpthread-stubs0-dev \
  libfcitx5core-dev libfcitx5utils-dev libfcitx5config-dev fcitx5-modules-dev \
  libboost-dev libboost-filesystem-dev libboost-system-dev libboost-regex-dev libboost-locale-dev \
  libyaml-cpp-dev libgoogle-glog-dev libleveldb-dev libmarisa-dev libopencc-dev >/dev/null 2>&1

STAGE=/src/stage22
rm -rf "$STAGE"; mkdir -p "$STAGE"

# librime + 插件 (已构建, 只 install)
cd /src/librime
rm -f build/install_manifest.txt
DESTDIR="$STAGE" cmake --install build >/dev/null 2>&1

# fcitx5-rime 5.0.11 (已构建, 只 install)
cd /src/fcitx5-rime-5.0.11
rm -f build/install_manifest.txt
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig
DESTDIR="$STAGE" cmake --install build >/dev/null 2>&1

echo "=== stage 完成 ==="
find "$STAGE" -name "*.so" -o -name "rime.conf" -o -name "*.metainfo.xml" | head -20
