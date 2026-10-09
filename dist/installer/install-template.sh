#!/bin/bash
# 蚌壳拼音 一键安装
#
# 双击本文件 → 文件管理器问「运行 / 显示」→ 选「运行」→ 弹出图形密码框 →
# 装好后**注销重登**，按 Ctrl+空格 就能切到「蚌壳拼音」。
set -u
NAME="__IM_NAME__"
cd "$(dirname "$0")" 2>/dev/null || true

DEB=$(ls fcitx5-rime-ai-predict_*.deb 2>/dev/null | head -1)
[ -z "$DEB" ] && DEB=$(ls *.deb 2>/dev/null | head -1)

has_gui=0
command -v zenity >/dev/null 2>&1 && has_gui=1
say()   { if [ "$has_gui" = 1 ]; then zenity --info  --title="$NAME" --text="$1" --width=420; else printf '%s\n' "$1"; fi; }
fail()  { if [ "$has_gui" = 1 ]; then zenity --error --title="$NAME" --text="$1" --width=420; else printf '错误：%s\n' "$1" >&2; fi; exit 1; }

[ -n "$DEB" ] || fail "没找到安装包（.deb）。请把「本脚本」和「.deb」放在同一个文件夹里再运行。"
[ -f "$DEB" ] || fail "找不到 $DEB"

[ "${1:-}" = "--terminal" ] && has_gui=0   # 从终端里跑的退路

# 图形化密码授权：zenity 弹密码框；没有 zenity 就开一个终端让用户在里面输。
if [ "${1:-}" != "--terminal" ] && [ "$has_gui" = 1 ]; then
  ASKPASS=$(mktemp)
  printf '#!/bin/sh\nexec zenity --password --title="%s"\n' "$NAME 安装：请输入登录密码" > "$ASKPASS"
  chmod +x "$ASKPASS"
  export SUDO_ASKPASS="$ASKPASS"
  SUDO="sudo -A"
elif [ "${1:-}" != "--terminal" ] && command -v gnome-terminal >/dev/null 2>&1; then
  gnome-terminal -- bash -c "\"$0\" --terminal; printf '\n按回车键关闭…'; read _" 2>/dev/null
  exit 0
else
  SUDO="sudo"
fi

say "正在安装，请稍候（包比较大，约 250MB）…"
$SUDO dpkg -i "$DEB" || { $SUDO apt-get -f install -y || true; $SUDO dpkg -i "$DEB" || fail "安装失败。请把终端/窗口里的报错发给我们。"; }
$SUDO ldconfig >/dev/null 2>&1 || true

say "安装完成 ✓

接下来请**注销并重新登录**（或重启电脑）。

登录后按 Ctrl+空格 就能切到「$NAME」。
想调外观或 AI 选项：在应用列表里搜「$NAME」打开设置。"
