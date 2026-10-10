#!/usr/bin/env bash
# 蚌壳拼音 —— Arch Linux ARM (aarch64) 安装脚本。
#
# 在**解开的包目录里**用 sudo 执行：sudo ./install-arch-aarch64.sh
#
# 与 deb 的 postinst 是同一套用户侧动作（seed / 生成主题 / 注册输入法），
# 差别只有两处 Arch 特有的地方，都写在下面的注释里。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

die() { echo "错误：$*" >&2; exit 1; }

[ "$(uname -m)" = "aarch64" ] || die "此包是 aarch64 专用，当前机器是 $(uname -m)"
[ -f /etc/arch-release ] || die "只支持 Arch 系发行版（找不到 /etc/arch-release）"
[ "$(id -u)" -eq 0 ] || die "请用 sudo 运行"
[ -d "$ROOT/usr" ] || die "请在解开的包目录里运行（当前 $ROOT 下没有 usr/）"

# 运行期依赖。fcitx5 是引擎本体，qt5-base 给设置程序用，其余是 librime 直接 NEEDED 的。
# 版本必须和构建时一致（滚动发行版里 pacman 会保证），所以不做捆绑。
DEPS=(fcitx5 qt5-base google-glog yaml-cpp gflags leveldb marisa opencc)

echo "==> 安装依赖：${DEPS[*]}"
pacman -S --needed --noconfirm "${DEPS[@]}"

echo "==> 落文件到 /"
# Arch 的库目录是 /usr/lib（不是 Ubuntu 的多架构目录），所以 rime.so 直接进 /usr/lib/fcitx5。
cp -a "$ROOT/usr/." /usr/
cp -a "$ROOT/etc/." /etc/
ldconfig

# OpenCC：Arch 只有 1.3.x，而 1.3 把带 `normalization` 的配置编成 PipelineConverter，
# 它的 GetConversionChain() **按设计返回 nullptr**，而 librime 的封装（写给 1.1 的）拿到就
# 直接 ->GetConversions() —— 段错误，且不是 C++ 异常，librime 的 catch(...) 兜不住，
# 表现为「一打字 fcitx5 就崩」。Ubuntu 那条线是靠捆 libopencc.so.1.1 绕开的。
# 这里不改发行版的 opencc：去掉 `normalization` 就退回普通 Converter，chain 正常。
# .ocd2 词库跟发行版走（软链过去），只有 json 配置要改格式。
echo "==> 生成 OpenCC 兼容配置"
OCC_SYS=/usr/share/opencc
OCC_DST=/usr/share/rime-data/opencc
[ -d "$OCC_SYS" ] || die "找不到 $OCC_SYS（opencc 包没装上？）"
rm -rf "$OCC_DST"; mkdir -p "$OCC_DST"
if command -v python3 >/dev/null 2>&1; then
  # 按**系统当前**的 opencc 配置现生成，避免发行版升级后配置与词库对不上。
  python3 - "$OCC_SYS" "$OCC_DST" <<'PY'
import json, glob, os, sys
src, dst = sys.argv[1], sys.argv[2]
n = 0
for f in glob.glob(os.path.join(src, "*.json")):
    d = json.load(open(f, encoding="utf-8"))
    d.pop("normalization", None)
    json.dump(d, open(os.path.join(dst, os.path.basename(f)), "w", encoding="utf-8"))
    n += 1
print(f"    {n} 个配置已转换")
PY
else
  # 没有 python3 就用包里带的成品（生成时对应的发行版版本见行尾注释）
  cp -a "$ROOT/usr/share/rime-data/opencc/." "$OCC_DST/"
fi
ln -sf "$OCC_SYS"/*.ocd2 "$OCC_DST/" 2>/dev/null || true
chmod -R a+rX /usr/share/rime-data
fc-cache -f >/dev/null 2>&1 || true
gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true

SEED=/usr/share/fcitx5-rime/seed
R=".local/share/fcitx5/rime"

# 只补用户没有的文件（-n）：升级时不能把用户已有的 *.custom.yaml（AI 设置、开关默认值）
# 用出厂值覆盖掉 —— 与 deb/mac 侧的 soft-write 行为一致。
seed() {
  local h="$1"; [ -d "$h" ] || return 0
  local t="$h/$R"; mkdir -p "$t"; cp -an "$SEED"/. "$t"/
  chown -R "$(stat -c '%U:%G' "$h")" "$t" 2>/dev/null || true
}

# 顺手把候选面板主题生成出来，用户不必先打开设置程序才有品牌外观。
gen_theme() {
  local h="$1"; [ -d "$h" ] || return 0
  HOME="$h" fcitx5-rime-config --generate-theme >/dev/null 2>&1 || true
  chown -R "$(stat -c '%U:%G' "$h")" "$h/.local/share/fcitx5/themes" "$h/.config/fcitx5" 2>/dev/null || true
}

# 把 rime 注册进 fcitx5 的输入法列表，并排到**第一位**、设为默认输入法。
#
# 为什么不能只"追加到末尾"（deb 的 postinst 是那么做的，但那假设的是全新 Ubuntu 用户）：
# Omarchy 预置的组里已经有 chewing（注音）。Ctrl+空格 是**按组内顺序向后枚举**的，追加到
# 末尾等于「按一下切到 chewing」。注音符号 ㄅㄆㄇㄈ 跟日文假名极像，用户会以为装成了日文
# 输入法，而且 chewing 打不出英文 —— 实测就是这么翻车的。
# 所以这里重写 group 0：rime 打头，其余输入法按原序保留在它后面。
register_im() {
  local h="$1"; [ -d "$h" ] || return 0
  local p="$h/.config/fcitx5/profile"
  mkdir -p "$h/.config/fcitx5"

  # 多组配置不动（重写会丢别的 group），退回"只设默认"。
  if [ -f "$p" ] && grep -q '^\[Groups/1\]' "$p"; then
    echo "    注意：profile 里有多个输入法组，只设默认、不改顺序" >&2
    sed -i 's/^DefaultIM=.*/DefaultIM=rime/' "$p" 2>/dev/null || true
    grep -q '^DefaultIM=' "$p" || sed -i 's/^\(Default Layout=.*\)$/\1\nDefaultIM=rime/' "$p"
    chown "$(stat -c '%U:%G' "$h")" "$p" 2>/dev/null || true
    return 0
  fi

  # 收集现有输入法名（只认 [Groups/0/Items/N] 节里的 Name=），保序去重
  local -a ims=(); local in_items=0 line name im seen
  if [ -f "$p" ]; then
    while IFS= read -r line || [ -n "$line" ]; do
      case $line in
        \[Groups/0/Items/*) in_items=1; continue ;;
        \[*)                in_items=0; continue ;;
      esac
      [ "$in_items" = 1 ] || continue
      case $line in Name=*) ;; *) continue ;; esac
      name=${line#Name=}
      seen=0
      for im in ${ims[@]+"${ims[@]}"}; do [ "$im" = "$name" ] && seen=1; done
      [ "$seen" = 1 ] || ims+=("$name")
    done < "$p"
  fi

  # rime 打头，其余按原序；确保至少留一个英文键盘
  local -a out=(rime); local has_kb=0
  for name in ${ims[@]+"${ims[@]}"}; do
    [ "$name" = rime ] && continue
    [ "$name" = keyboard-us ] && has_kb=1
    out+=("$name")
  done
  [ "$has_kb" = 1 ] || out+=(keyboard-us)

  local tmp="$p.new" i=0
  {
    printf '[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=rime\n\n'
    for name in "${out[@]}"; do
      printf '[Groups/0/Items/%s]\nName=%s\nLayout=\n\n' "$i" "$name"
      i=$((i + 1))
    done
    printf '[GroupOrder]\n0=Default\n'
  } > "$tmp"
  mv "$tmp" "$p"
  chown "$(stat -c '%U:%G' "$h")" "$p" 2>/dev/null || true
}

echo "==> 写用户配置"
seed /etc/skel; gen_theme /etc/skel; register_im /etc/skel
if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then
  H="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
  seed "$H"; gen_theme "$H"; register_im "$H"
  echo "    已为用户 $SUDO_USER 生成 seed / 主题 / 输入法注册"
fi

echo
echo "==> 装好了。接下来："
echo "    1. 让 fcitx5 重新加载：  fcitx5 -r -d    （或直接重登一次会话）"
echo "    2. 按 Ctrl+空格 切到「蚌壳拼音」，打拼音试候选"
echo
echo "    排查用：fcitx5-diagnose | grep -A5 'Addon List'"
