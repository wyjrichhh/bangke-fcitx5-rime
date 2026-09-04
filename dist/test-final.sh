#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq fcitx5 >/dev/null 2>&1

echo "=== 安装 .deb ==="
dpkg -i /src/fcitx5-rime-ai-predict_5.0.11_amd64.deb 2>&1 | tail -2
echo "dpkg 退出码: $?"

echo "=== ldconfig ==="
ldconfig

echo "=== addon rime.so 依赖 (librime 应解析到私有目录, fcitx5 到系统) ==="
ldd /usr/lib/x86_64-linux-gnu/fcitx5/rime.so 2>&1 | grep -iE "librime|Fcitx5|not found"

echo "=== librime.so 依赖 (捆绑库) ==="
ldd /usr/lib/fcitx5-rime-ai/librime.so.1.16.1 2>&1 | grep -iE "boost|icu|yaml|glog|not found" | head -6

echo "=== 汇总: not found 数量 ==="
for f in /usr/lib/x86_64-linux-gnu/fcitx5/rime.so /usr/lib/fcitx5-rime-ai/librime.so.1.16.1 /usr/lib/fcitx5-rime-ai/rime-plugins/librime-ai-predict.so; do
  m=$(ldd "$f" 2>&1 | grep -c "not found" || true)
  echo "$(basename $f): not found = $m"
done

echo "=== fcitx5 是否识别 rime ==="
fcitx5-diagnose 2>&1 | grep -iE "Rime|addon config directory" | head -3
