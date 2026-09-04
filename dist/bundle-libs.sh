#!/bin/bash
set -e
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq libboost-regex1.74-dev libboost-filesystem1.74-dev libboost-system1.74-dev libboost-locale1.74-dev libyaml-cpp-dev libgoogle-glog-dev libleveldb-dev libmarisa-dev libopencc-dev libgflags-dev libsnappy-dev >/dev/null 2>&1

SEEN=/tmp/seen.txt
rm -f "$SEEN"
BUNDLE=/src/bundle-libs
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

collect /src/librime/build/lib/librime.so.1.16.1
collect /src/librime/build/lib/rime-plugins/librime-ai-predict.so

echo "=== 全部依赖 (排除系统库) ==="
grep -vE 'libc\.so|libstdc\+\+|libm\.so|libpthread|libdl\.so|librt\.so|libgcc_s|ld-linux|libfcitx|libdbus|libxkb|libFcitx|libsystemd|libcap\.so|libgcrypt|libgpg|liblzma|liblz4|libzstd|libuv|libunwind' "$SEEN" | sort -u

echo "=== 复制进 bundle ==="
grep -vE 'libc\.so|libstdc\+\+|libm\.so|libpthread|libdl\.so|librt\.so|libgcc_s|ld-linux|libfcitx|libdbus|libxkb|libFcitx|libsystemd|libcap\.so|libgcrypt|libgpg|liblzma|liblz4|libzstd|libuv|libunwind' "$SEEN" | sort -u | while read -r p; do
  cp -L "$p" "$BUNDLE/" 2>/dev/null || true
done
echo "=== bundle 内容 ==="
ls -la "$BUNDLE/"
