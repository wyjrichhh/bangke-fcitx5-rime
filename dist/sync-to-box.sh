#!/bin/bash
# 把 bangke 侧的三个私有库同步到构建机的工作树：
#
#   bangke-fcitx5-rime  → ~/bangke/commit/fcitx5-rime   （addon + config-ui + 种子）
#   bangke-librime      → ~/bangke/commit/bangke-librime
#   bangke-ai-predict   → ~/bangke/commit/bangke-ai-predict
#
# 构建机连不上 GitHub，也没有这几个仓库的凭据，所以一律 tar-over-ssh 推过去。
# 只推工作树、不删远端多余文件（增量文件不会消失）。
# COPYFILE_DISABLE=1 挡掉 macOS 的 AppleDouble（._*）与扩展属性头 —— 少了它
# Linux 那边会把 ._*.yaml 当真配置解析。
#
# 用法：bash dist/sync-to-box.sh [user@host]
set -eu

HOST="${1:-rich@192.168.101.17}"
BOX="${BOX:-/home/rich/bangke/commit}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORKSPACE="$(cd "$REPO/.." && pwd)"

push() {
  local name="$1" local_dir="$2" remote_dir="$3"; shift 3
  [ -d "$local_dir" ] || { echo "!! 找不到 $local_dir" >&2; return 1; }
  echo "==> $name → $HOST:$remote_dir"
  ssh "$HOST" "mkdir -p '$remote_dir'"
  COPYFILE_DISABLE=1 tar -C "$local_dir" -cf - \
      --exclude=.git --exclude=build --exclude='._*' --exclude='*.o' \
      --exclude=build-out "$@" . |
    ssh "$HOST" "tar -C '$remote_dir' -xf -"
}

push "bangke-fcitx5-rime" "$REPO"                       "$BOX/fcitx5-rime"
# librime 的 include/ 里有构建必需的 vendored 头（darts/utf8/X11），不能整目录排掉 ——
# 只排 `make deps` 从 Mac 带过去的那些第三方头，它们在 Linux 上会和系统库打架。
push "bangke-librime"    "$WORKSPACE/bangke-librime"    "$BOX/bangke-librime" \
    --exclude=lib --exclude=deps \
    --exclude='include/glog' --exclude='include/yaml-cpp' --exclude='include/leveldb' \
    --exclude='include/marisa' --exclude='include/opencc' --exclude='include/gtest'
# 插件仓库的 lib//include/ 是 macOS 侧 `make deps` 的静态产物，Linux 上自己编 CT2。
push "bangke-ai-predict" "$WORKSPACE/bangke-ai-predict" "$BOX/bangke-ai-predict" \
    --exclude=lib --exclude=include --exclude=deps

echo "==> 完成"
ssh "$HOST" "for d in fcitx5-rime bangke-librime bangke-ai-predict; do
  printf '%-20s ' \"\$d\"; ls -d '$BOX'/\"\$d\" >/dev/null 2>&1 && echo ok || echo 缺
done
ls -la '$BOX/fcitx5-rime/data/ai-predict-defaults' 2>/dev/null | head -5"
