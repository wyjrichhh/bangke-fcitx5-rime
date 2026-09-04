#!/bin/bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

echo "=== [1/5] 安装 Ubuntu 22.04 依赖 ==="
apt-get update -qq
apt-get install -y -qq \
  cmake ninja-build build-essential pkg-config ca-certificates \
  extra-cmake-modules gettext libpthread-stubs0-dev \
  libfcitx5core-dev libfcitx5utils-dev libfcitx5config-dev fcitx5-modules-dev \
  libboost-dev libboost-filesystem-dev libboost-system-dev libboost-regex-dev libboost-locale-dev \
  libyaml-cpp-dev libgoogle-glog-dev libleveldb-dev libmarisa-dev libopencc-dev \
  qtbase5-dev

echo "=== [2/5] 构建 CTranslate2 (静态) ==="
cd /src/librime/plugins/librime-ai-predict
make -f deps.mk ctranslate2

echo "=== [3/5] 构建 librime 1.16.1 + 插件 ==="
cd /src/librime
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=ON -DBUILD_MERGED_PLUGINS=OFF -DENABLE_EXTERNAL_PLUGINS=ON \
  -DBUILD_TEST=OFF -DBUILD_SAMPLE=OFF
ninja -C build -j "$(nproc)"
cmake --install build

echo "=== [4/5] 构建 fcitx5-rime 5.0.11 (移植版) ==="
cd /src/fcitx5-rime-5.0.11
sed -i 's/^fcitx5_install_translation/# disabled: gettext 0.21 .mo.rule bug/' po/CMakeLists.txt
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/usr/local -DRIME_DATA_DIR=/usr/share/rime-data
ninja -C build -j "$(nproc)"

echo "=== [5/5] 构建 config-ui (Qt5) ==="
cd /src/config-ui
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
ninja -C build -j "$(nproc)"

echo "=== 构建完成, 产物 ==="
find /src/fcitx5-rime-5.0.11/build -name "rime.so"
find /src/librime/build -name "librime-ai-predict.so" -o -name "librime.so.1.16.1"
ls -l /src/config-ui/build/fcitx5-rime-config
