#!/bin/bash
# 收拢 librime 与插件动态链到的第三方 .so，随包分发（目标机器不保证装齐这些包）。
# 在 build-22.04.sh 跑过之后、同一个 ubuntu:22.04 容器里执行：
#
#   docker run --rm -v ~/bangke/commit/fcitx5-rime:/priv \
#     -v ~/bangke/build-out/22.04:/out \
#     ubuntu:22.04 bash /priv/dist/bundle-libs.sh
#
# 产物落 /out/bundle-libs（构建产物，与 vendor 的第三方物料分开）。
set -e
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq libboost-regex1.74-dev libboost-filesystem1.74-dev libboost-system1.74-dev libboost-locale1.74-dev libyaml-cpp-dev libgoogle-glog-dev libleveldb-dev libmarisa-dev libopencc-dev libgflags-dev libsnappy-dev >/dev/null 2>&1

SEEN=/tmp/seen.txt
rm -f "$SEEN"
BUNDLE="${BUNDLE:-/out/bundle-libs}"
rm -rf "$BUNDLE"; mkdir -p "$BUNDLE"

collect() {
  local f="$1"
  ldd "$f" 2>/dev/null | grep -oE '/[^ ]+\.so[.0-9]*' | while read -r p; do
    if ! grep -qxF "$p" "$SEEN" 2>/dev/null; then
      echo "$p" >> "$SEEN"
      collect "$p"
    fi
  done
}

# 版本号不写死：librime 的 so 名带次版本（librime.so.1.17.0），换版本时这里不该跟着改。
LIBRIME_SO=$(ls /out/librime/build/lib/librime.so.1.* 2>/dev/null | head -1)
[ -n "$LIBRIME_SO" ] || { echo "找不到编出来的 librime.so.1.*" >&2; exit 1; }
collect "$LIBRIME_SO"
collect /out/librime/build/lib/rime-plugins/librime-ai-predict.so
collect /out/lib/libctranslate2.so.4.7.1

echo "=== 全部依赖 (排除系统库) ==="
grep -vE 'libc\.so|libstdc\+\+|libm\.so|libpthread|libdl\.so|librt\.so|libgcc_s|ld-linux|libfcitx|libdbus|libxkb|libFcitx|libsystemd|libcap\.so|libgcrypt|libgpg|liblzma|liblz4|libzstd|libuv|libunwind' "$SEEN" | sort -u

echo "=== 复制进 bundle ==="
grep -vE 'libc\.so|libstdc\+\+|libm\.so|libpthread|libdl\.so|librt\.so|libgcc_s|ld-linux|libfcitx|libdbus|libxkb|libFcitx|libsystemd|libcap\.so|libgcrypt|libgpg|liblzma|liblz4|libzstd|libuv|libunwind' "$SEEN" | sort -u | while read -r p; do
  cp -L "$p" "$BUNDLE/" 2>/dev/null || true
done
echo "=== bundle 内容 ==="
ls -la "$BUNDLE/"
