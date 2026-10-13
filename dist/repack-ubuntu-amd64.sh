#!/bin/bash
# Ubuntu amd64 出包（容器侧）。必须在 ubuntu:22.04 里跑：
#
#   docker run --rm \
#     -v ~/bangke/commit/fcitx5-rime:/priv \
#     -v ~/bangke/commit/bangke-librime:/librime:ro \
#     -v ~/bangke/commit/bangke-ai-predict:/plugin:ro \
#     -v ~/bangke/vendor:/vendor:ro \
#     -v ~/bangke/build-out/22.04:/out \
#     ubuntu:22.04 bash /priv/dist/repack-ubuntu-amd64.sh
#
# 与 build-22.04.sh 的差别只有一个，但很关键：/priv/build 是 Ubuntu 线与 Arch 线
# 共用的构建树，可能残留 Arch 编的 addon。Arch 的 fcitx5 比发行版新，addon 链的是
# registerDomain(path) 重载，打进 Ubuntu 包后在 24.04 上 dlopen 失败、fcitx5 开机即退。
# 所以这里无条件重编 addon 与 config-ui，再出包。
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
apt-get install -y -qq \
  cmake ninja-build build-essential pkg-config ca-certificates \
  extra-cmake-modules gettext libpthread-stubs0-dev \
  libfcitx5core-dev libfcitx5utils-dev libfcitx5config-dev fcitx5-modules-dev \
  libboost-dev libboost-filesystem-dev libboost-system-dev libboost-regex-dev libboost-locale-dev \
  libyaml-cpp-dev libgoogle-glog-dev libleveldb-dev libmarisa-dev libopencc-dev \
  liblua5.4-dev qtbase5-dev

echo "=== [0/4] 复用 /out 的 CT2 与 librime 构建树，装进 /usr/local ==="
cmake --build /out/ct2/build --target install
cmake --install /out/librime/build --prefix /usr/local

echo "=== [1/4] 重编 fcitx5 addon + config-ui ==="
SKIP_LIBRIME=1 bash /priv/dist/build-22.04.sh

echo "=== [2/4] 重编 librime 插件 ==="
cp -a /plugin/src/. /out/librime/plugins/librime-ai-predict/src/
cmake --build /out/librime/build --target rime-ai-predict

echo "=== [3/4] 把关：addon 必须链 22.04 的 char* 重载 ==="
# 先把 nm 的输出收完再比：`nm | grep -q` 会在命中时让 nm 吃 SIGPIPE，配合 pipefail 反而误判成失败。
SYMS=$(nm -D /priv/build/src/rime-ai.so || true)
case "$SYMS" in
  *registerDomainEPKcS1_*) echo "addon 链的是 registerDomain(char const*, char const*)" ;;
  *)
    echo "!! addon 没链上 22.04 fcitx5 的 registerDomain(char const*, char const*)，拒绝出包" >&2
    exit 1 ;;
esac

echo "=== [4/4] 出包 ==="
bash /priv/dist/repack-container.sh
