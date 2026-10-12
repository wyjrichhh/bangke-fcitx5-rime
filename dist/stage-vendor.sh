#!/bin/bash
# 把「第三方与自训大件」收拢到 ~/bangke/vendor —— 出包用的物料只有两个来源：
# bangke 侧的私有库（我们的代码与配置）和这里（不是我们写的、或者太大不适合进 git）。
#
# 在构建机上跑（那里才有这些物料）。幂等：已在 vendor 里的不重复搬。
#
#   bash ~/bangke/commit/fcitx5-rime/dist/stage-vendor.sh [--from <旧物料区>]
#
# 旧物料区默认是 ~/bangke/openSource（这次迁移的来源，迁完就不再是事实来源）。
# 模型 230MB+，一律 mv 或 hardlink，不复制。
set -euo pipefail

VENDOR="${VENDOR:-$HOME/bangke/vendor}"
FROM="${FROM:-$HOME/bangke/openSource}"
while [ $# -gt 0 ]; do
  case "$1" in
    --from) FROM="$2"; shift 2 ;;
    *) echo "未知参数：$1" >&2; exit 2 ;;
  esac
done

mkdir -p "$VENDOR"

# 搬一棵树：目标已存在就跳过（幂等）。先试 mv（同一文件系统、瞬间完成且不占额外空间），
# 跨文件系统时退回 cp -al（hardlink），再不行才真复制。
place() {
  local src="$1" dst="$2"
  if [ -e "$dst" ]; then
    echo "  已有 $dst，跳过"
    return 0
  fi
  [ -e "$src" ] || { echo "!! 缺来源 $src" >&2; return 1; }
  mkdir -p "$(dirname "$dst")"
  if mv "$src" "$dst" 2>/dev/null; then
    echo "  移入 $dst"
  elif cp -al "$src" "$dst" 2>/dev/null; then
    echo "  硬链入 $dst（来源保留在 $src）"
  else
    cp -a "$src" "$dst"
    echo "  复制入 $dst"
  fi
}

# 第一个存在的候选就是来源。CT2 源码在不同阶段的线里落点不一样，别指望只有一个。
place_first() {
  local dst="$1"; shift
  if [ -e "$dst" ]; then
    echo "  已有 $dst，跳过"
    return 0
  fi
  local s
  for s in "$@"; do
    if [ -e "$s" ]; then
      place "$s" "$dst" && return 0
    fi
  done
  echo "!! 这些候选都没有：$*" >&2
  return 1
}

echo "==> 物料区 $VENDOR（来源 $FROM）"

# CTranslate2 源码。ruy 的 cpuinfo 子模块构建机取不到（连不上 GitHub），
# 当年靠 scp 暂存在 gh-stage，跟着一起摆好。
place_first "$VENDOR/ctranslate2" \
  "$HOME/bangke/ct2-src/CTranslate2" \
  "$FROM/librime/plugins/librime-ai-predict/deps/CTranslate2" \
  "$HOME/bangke/plugin-src/librime-ai-predict/deps/CTranslate2" || true
if [ ! -f "$VENDOR/ctranslate2/third_party/ruy/third_party/cpuinfo/CMakeLists.txt" ]; then
  place "$HOME/bangke/gh-stage/cpuinfo" "$VENDOR/ctranslate2/third_party/ruy/third_party/cpuinfo" \
    || echo "  !! 仍缺 ruy/cpuinfo —— 在 Mac 上 clone 后同步过来（构建机连不上 GitHub）" >&2
fi

# 雾凇方案依赖 librime-lua，同样是构建机上 clone 不到的。
place "$HOME/bangke/gh-stage/librime-lua" "$VENDOR/librime-lua" || true

place "$FROM/deb-resources/predict_models/zh-base-ct2-int8" "$VENDOR/models/zh-base-ct2-int8" || true
place "$FROM/deb-resources/fonts"  "$VENDOR/fonts"  || true
place "$FROM/deb-resources/rime-ice" "$VENDOR/rime-ice" || true
# AI 种子的词库分片（45MB，太大不进 git，与私有库里的 *.custom.yaml 配套）。
place "$FROM/deb-resources/ai-predict-defaults/dicts" "$VENDOR/dicts" || true
# 注：捆绑库（bundle-libs）是构建产物、由 bundle-libs.sh 写进 /out，不放这里。

echo
echo "==> 自检"
missing=0
check() { if [ -e "$1" ]; then echo "  ✓ $1"; else echo "  ✗ 缺 $1"; missing=$((missing + 1)); fi; }
check "$VENDOR/ctranslate2/third_party/ruy/third_party/cpuinfo/CMakeLists.txt"
check "$VENDOR/librime-lua"
check "$VENDOR/models/zh-base-ct2-int8/model.bin"
check "$VENDOR/fonts"
check "$VENDOR/rime-ice"
check "$VENDOR/dicts"

if [ "$missing" -ne 0 ]; then
  echo
  echo "有 $missing 项没到位 —— 三条打包线都会在对应步骤报错，先补齐再出包。" >&2
  exit 1
fi
echo
echo "物料齐了。打包时挂 -v $VENDOR:/vendor:ro"
