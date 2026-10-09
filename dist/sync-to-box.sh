#!/bin/bash
# 把本仓库（**私有库**）同步到构建机的私有库工作树：~/bangke/commit/fcitx5-rime
#
# 约定（2026-10 起）：
#   私有库 = 本仓库 / ~/bangke/commit/fcitx5-rime   ← 改动只进这里
#   开源库 = ~/dev/open-source/fcitx5-rime / ~/bangke/openSource  ← 定型，不往里写
#
# 构建机上不是本地开发机（国内网络直连 GitHub 不稳、也没配凭据），所以用
# tar-over-ssh 覆盖工作树 —— 保留那边的 .git，不做删除（增量文件不会消失）。
# COPYFILE_DISABLE=1 挡掉 macOS 的 AppleDouble（._*）与扩展属性头。
set -eu

HOST="${1:-rich@192.168.101.17}"
PRIV="${PRIV:-/home/rich/bangke/commit/fcitx5-rime}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"

echo "==> 同步私有库工作树 → $HOST:$PRIV"
ssh "$HOST" "mkdir -p '$PRIV'"
COPYFILE_DISABLE=1 tar -C "$REPO" -cf - \
    --exclude=.git --exclude=build --exclude='._*' --exclude='*.o' . |
  ssh "$HOST" "tar -C '$PRIV' -xf -"

echo "==> 完成"
ssh "$HOST" "cd '$PRIV' && git status --short | head -8; echo '---'; ls config-ui/src/theme | head -4; ls -la data/bangke.yaml"
