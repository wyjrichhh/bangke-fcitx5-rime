#!/bin/bash
# 在 ubuntu:22.04 容器里构建（用最老的发行版做基线，glibc 向后兼容）。
#
# 全部输入都来自 bangke 侧的私有库与 vendor 暂存区，不再依赖 openSource：
#   /priv    私有库 ~/bangke/commit/fcitx5-rime      （addon + config-ui + data/ai-predict-defaults）
#   /librime 私有库 ~/bangke/commit/bangke-librime    （已含 22.04 兼容补丁）
#   /plugin  私有库 ~/bangke/commit/bangke-ai-predict （AI 插件）
#   /vendor  暂存区 ~/bangke/vendor                   （第三方与自训大件，见 dist/stage-vendor.sh）
#   /out     产物区 ~/bangke/build-out/22.04          （跨容器持久化；repack-container.sh 读它）
#
#   docker run --rm \
#     -v ~/bangke/commit/fcitx5-rime:/priv \
#     -v ~/bangke/commit/bangke-librime:/librime:ro \
#     -v ~/bangke/commit/bangke-ai-predict:/plugin:ro \
#     -v ~/bangke/vendor:/vendor:ro \
#     -v ~/bangke/build-out/22.04:/out \
#     ubuntu:22.04 bash /priv/dist/build-22.04.sh
#
# 只改了私有代码（比如 config-ui）时用 SKIP_LIBRIME=1 跳过最慢的 librime 重建
# （librime 与 CT2 的构建树留在 /out 里复用）。
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
SKIP_LIBRIME="${SKIP_LIBRIME:-0}"

echo "=== [1/5] 安装 Ubuntu 22.04 依赖 ==="
apt-get update -qq
apt-get install -y -qq \
  cmake ninja-build build-essential pkg-config ca-certificates \
  extra-cmake-modules gettext libpthread-stubs0-dev \
  libfcitx5core-dev libfcitx5utils-dev libfcitx5config-dev fcitx5-modules-dev \
  libboost-dev libboost-filesystem-dev libboost-system-dev libboost-regex-dev libboost-locale-dev \
  libyaml-cpp-dev libgoogle-glog-dev libleveldb-dev libmarisa-dev libopencc-dev \
  liblua5.4-dev \
  qtbase5-dev

# 第三方工程里还写着老的 cmake 版本号，统一放行，不改上游源码。
CMAKE_OLD_POLICY=(-DCMAKE_POLICY_VERSION_MINIMUM=3.5)

mkdir -p /out

# ---------------------------------------------------------------- CTranslate2
# **必须是共享库**，且 RUY 打开：插件链的是 libctranslate2.so.4（ruy 编在里面）。
# 编成静态插件会留一堆未定义的 ruy 符号，表现是 addon 能加载、AI 预测静默失效。
# 这也是这里不用插件自带 deps.mk 的原因 —— 那份是 macOS 侧的静态用法。
if [ "$SKIP_LIBRIME" = "1" ] && [ -d /out/ct2/build ]; then
  echo "=== [2-3/5] 复用 /out 里的 CT2 与 librime 构建树 ==="
  cmake --build /out/ct2/build --target install
  cmake --install /out/librime/build --prefix /usr/local
else
  echo "=== [2/5] 构建 CTranslate2 (共享, Ruy 后端) ==="
  # ruy 的 cpuinfo 子模块在构建机取不到（连不上 GitHub），随 vendor 一起放好。
  [ -f /vendor/ctranslate2/third_party/ruy/third_party/cpuinfo/CMakeLists.txt ] || {
    echo "缺 /vendor/ctranslate2/third_party/ruy/third_party/cpuinfo（见 dist/stage-vendor.sh）" >&2; exit 1; }
  rm -rf /out/ct2
  cp -a /vendor/ctranslate2 /out/ct2
  cd /out/ct2
  cmake . -Bbuild -G Ninja -DBUILD_SHARED_LIBS=ON -DBUILD_CLI=OFF -DBUILD_TESTS=OFF \
    -DWITH_CUDA=OFF -DWITH_MKL=OFF -DWITH_OPENBLAS=OFF -DWITH_RUY=ON -DWITH_ACCELERATE=OFF \
    -DOPENMP_RUNTIME=NONE -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_INSTALL_RPATH=/usr/lib/fcitx5-rime-ai -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    "${CMAKE_OLD_POLICY[@]}"
  cmake --build build -j "$(nproc)"
  cmake --build build --target install

  echo "=== [3/5] 构建 librime + 插件 ==="
  # 在容器里组树，不动宿主的私有库（挂载是只读的）。
  rm -rf /out/librime
  cp -a /librime /out/librime
  # 私有库里带着 macOS 侧 `make deps` 的产物，Linux 上必须清掉：
  #   lib/*.a 是 Mach-O 归档，链接器会把它们当依赖去找符号，然后全部 undefined；
  #   include/ 下的第三方旧头会和系统库 API 对不上（LINUX_BUILD.md 补丁 5）。
  rm -rf /out/librime/lib /out/librime/build
  rm -rf /out/librime/include/glog /out/librime/include/yaml-cpp \
         /out/librime/include/leveldb /out/librime/include/marisa \
         /out/librime/include/opencc /out/librime/include/gtest
  rm -rf /out/librime/plugins/librime-ai-predict
  mkdir -p /out/librime/plugins/librime-ai-predict
  cp -a /plugin/. /out/librime/plugins/librime-ai-predict/
  rm -rf /out/librime/plugins/librime-ai-predict/deps \
         /out/librime/plugins/librime-ai-predict/build
  cd /out/librime
  rm -rf build
  # 雾凇方案依赖 librime-lua，缺了会刷一屏 "error creating translator"。
  # 它按 pkg-config 名字找 lua/lua54/lua53/…，而 Ubuntu 的包名是 lua5.4，
  # 用 LUA_VERSION 把第一个候选指过去。
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_SHARED_LIBS=ON -DBUILD_MERGED_PLUGINS=OFF -DENABLE_EXTERNAL_PLUGINS=ON \
    -DBUILD_TEST=OFF -DBUILD_SAMPLE=OFF -DCTRANSLATE2_ROOT=/usr/local \
    -DLUA_VERSION=lua5.4 "${CMAKE_OLD_POLICY[@]}"
  ninja -C build -j "$(nproc)"
  cmake --install build
fi

# 插件里不该残留 ruy 符号 —— 留着就是上面那个静默失效。编完必须查。
SO=/out/librime/build/lib/rime-plugins/librime-ai-predict.so
RUY_UNDEF=$(nm -D --undefined-only "$SO" | grep -c ruy || true)
[ "$RUY_UNDEF" -eq 0 ] || { echo "插件残留 $RUY_UNDEF 个未定义 ruy 符号，AI 预测会失效" >&2; exit 1; }

# 打包要用的第三方产物，从容器里落到持久区（容器是一次性的）。
mkdir -p /out/lib
cp -a /usr/local/lib/libctranslate2.so* /out/lib/

echo "=== [4/5] 构建 fcitx5-rime addon (私有移植版) ==="
cd /priv
# gettext 0.21 的 .mo.rule bug：22.04 上必须停掉翻译安装（幂等）
if grep -q '^fcitx5_install_translation' po/CMakeLists.txt; then
  sed -i 's/^fcitx5_install_translation/# disabled: gettext 0.21 .mo.rule bug/' po/CMakeLists.txt
fi
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/usr/local -DRIME_DATA_DIR=/usr/share/rime-data "${CMAKE_OLD_POLICY[@]}"
ninja -C build -j "$(nproc)"

echo "=== [5/5] 构建 config-ui (Qt5) ==="
rm -rf /priv/config-ui/build
cmake -S /priv/config-ui -B /priv/config-ui/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
ninja -C /priv/config-ui/build -j "$(nproc)"

echo "=== 构建完成, 产物 ==="
ls -l "$SO" /out/lib/libctranslate2.so.4* /out/librime/build/lib/librime.so.1.*
find /priv/build -name "rime.so"
ls -l /priv/config-ui/build/fcitx5-rime-config
