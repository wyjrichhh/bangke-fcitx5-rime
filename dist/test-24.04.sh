#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
echo "=== 装 fcitx5 (24.04 自带 5.1.7) ==="
apt-get install -y -qq fcitx5 >/dev/null 2>&1
dpkg -l fcitx5 | grep fcitx5 | awk '{print "fcitx5 版本:", $3}'

echo "=== 安装 .deb ==="
dpkg -i /src/fcitx5-rime-ai-predict_5.0.11_amd64.deb 2>&1 | tail -5
echo "dpkg 退出码: $?"

echo "=== ldconfig ==="
ldconfig

echo "=== addon rime.so 对 fcitx5 的解析 ==="
ldd /usr/local/lib/fcitx5/rime.so 2>&1 | grep -iE "Fcitx5|not found"

echo "=== librime.so 对捆绑库的解析 ==="
ldd /usr/local/lib/librime.so.1.16.1 2>&1 | grep -iE "boost|icu|yaml|glog|leveldb|marisa|opencc|snappy|gflags|not found"

echo "=== 插件解析 ==="
ldd /usr/local/lib/rime-plugins/librime-ai-predict.so 2>&1 | grep -iE "librime|boost|icu|not found"

echo "=== 汇总: 每个 .so 的 not found 数量 ==="
for f in /usr/local/lib/fcitx5/rime.so /usr/local/lib/librime.so.1.16.1 /usr/local/lib/rime-plugins/librime-ai-predict.so; do
  missing=$(ldd "$f" 2>&1 | grep -c "not found" || true)
  echo "$(basename $f): not found = $missing"
done
