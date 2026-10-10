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

# 把 rime 注册进 fcitx5 的输入法列表。装完 addon 只是"可用"，还得进输入法组才会出现。
# 必须插在 [GroupOrder] 之前：profile 是分节 INI，追加到末尾会落进那一节里，等于没注册。
register_im() {
  local h="$1"; [ -d "$h" ] || return 0
  local p="$h/.config/fcitx5/profile"
  if [ -f "$p" ] && grep -q '^Name=rime$' "$p"; then return 0; fi
  mkdir -p "$h/.config/fcitx5"
  if [ ! -f "$p" ]; then
    printf '[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=rime\n\n[Groups/0/Items/0]\nName=rime\nLayout=\n\n[GroupOrder]\n0=Default\n' > "$p"
  else
    # 组里已有几项就接在几号后面
    local n item
    n=$(grep -c '^\[Groups/0/Items/' "$p" 2>/dev/null || true)
    item=$(printf '[Groups/0/Items/%s]\nName=rime\nLayout=\n' "${n:-0}")
    if grep -q '^\[GroupOrder\]' "$p"; then
      # 逐行重写、插在 [GroupOrder] 之前。
      # 不能用 awk -v：带换行的值会报 "newline in string"（deb 侧实测踩过）。
      local tmp="$p.new"; : > "$tmp"
      while IFS= read -r line || [ -n "$line" ]; do
        [ "$line" = "[GroupOrder]" ] && printf '%s\n\n' "$item" >> "$tmp"
        printf '%s\n' "$line" >> "$tmp"
      done < "$p"
      mv "$tmp" "$p"
    else
      printf '%s\n\n' "$item" >> "$p"
    fi
  fi
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
