#!/bin/bash
# 候选面板的截图验收 harness。
#
# 在一台有 Xvfb 与 fcitx5 的机器（或容器）里跑：起一个虚拟 X、拉起真 fcitx5 +
# 真 classicui，用一个 GTK3 客户端打出拼音，截下整屏。用来验证「我们生成的主题
# 在真面板上长什么样」，并与设置页预览对账。
#
# 用法（容器里，/out 是挂进来的目录）：
#   panel-harness.sh <输出png> [要打的字] [主题名]
# 主题名非空时，把 /out/themes/<主题名> 装成 classicui 的主题；为空则用内置主题。
#
# 两个必须知道的坑：
#   1. 输入法组（profile）必须在 fcitx5 **停止时**写 —— 运行中的 fcitx5 会把
#      它不认识的项剔掉，重启后又把文件写回去，表现为「rime 加不进去」。
#   2. 会话总线要用固定地址（--address），否则后续 fcitx5-remote 之类连不上。
set -u

OUT="${1:-/out/shot.png}"
TEXT="${2:-bangke}"
THEME="${3:-}"
BUS=/tmp/bkdbus
HOME_DIR="${HOME:-/root}"

export DISPLAY=:99
pkill -x fcitx5 2>/dev/null
sleep 0.5
pkill -x mousepad 2>/dev/null
sleep 0.3

# 主题：装到用户主题目录，并让 classicui.conf 指过去
rm -rf "$HOME_DIR/.local/share/fcitx5/themes"
mkdir -p "$HOME_DIR/.local/share/fcitx5/themes" "$HOME_DIR/.config/fcitx5/conf"
if [ -n "$THEME" ] && [ -d "/out/themes/$THEME" ]; then
  cp -a /out/themes/. "$HOME_DIR/.local/share/fcitx5/themes/"
  cp "/out/themes/$THEME/classicui.conf" "$HOME_DIR/.config/fcitx5/conf/classicui.conf" 2>/dev/null ||
    printf 'Theme=%s\n' "$THEME" > "$HOME_DIR/.config/fcitx5/conf/classicui.conf"
else
  rm -f "$HOME_DIR/.config/fcitx5/conf/classicui.conf"
fi

# 输入法组（务必在 fcitx5 停止时写，见文件头坑 1）
cat > "$HOME_DIR/.config/fcitx5/profile" <<'PROFILE'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=rime-ai

[Groups/0/Items/0]
Name=rime-ai
Layout=

[Groups/0/Items/1]
Name=keyboard-us
Layout=

[GroupOrder]
0=Default
PROFILE

pkill -f 'Xvfb :99' 2>/dev/null
sleep 0.3
Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 &
sleep 1.5

# 合成器：没有它，面板圆角处的透明像素会直接落成黑色（Xvfb 无合成），
# 会把圆角误读成「黑色残迹」。
if command -v xcompmgr >/dev/null 2>&1; then
  pkill -x xcompmgr 2>/dev/null
  sleep 0.3
  xcompmgr -c >/dev/null 2>&1 &
  sleep 1
fi

pkill -f "dbus-daemon --session --address=unix:path=$BUS" 2>/dev/null
rm -f "$BUS"
dbus-daemon --session --address="unix:path=$BUS" --fork >/dev/null 2>&1
export DBUS_SESSION_BUS_ADDRESS="unix:path=$BUS"

export LANG=C.UTF-8 LC_ALL=C.UTF-8
# GTK 的模块名：22.04 只有 im-fcitx5.so，24.04 两个都有 —— 按实际存在的选，
# 否则客户端根本不会把按键交给 fcitx5（表现为"打字直接进输入框、没有候选"）。
GTK_MOD=fcitx
for d in /usr/lib/*/gtk-3.0/3.0.0/immodules /usr/lib/gtk-3.0/3.0.0/immodules; do
  [ -e "$d/im-fcitx5.so" ] && GTK_MOD=fcitx5 && break
done
export XMODIFIERS=@im=fcitx GTK_IM_MODULE="$GTK_MOD" QT_IM_MODULE=fcitx
rm -f /tmp/fcitx5.log
# BK_UI=名称 时指定 UI 插件（例如 BK_UI=bangkeui 用自绘面板）
fcitx5 -d ${BK_UI:+-u "$BK_UI"} >/tmp/fcitx5.log 2>&1
sleep 4

# 客户端用 GTK3 的 mousepad：GTK 会宣告 preedit 能力，拼音才会进应用、
# 面板里就没有拼音行 —— 这正是「高亮在第一行、上沿该被面板圆角裁出来」的场景。
# （xterm 走 XIM 没有 preedit 能力，面板永远带拼音行，验不了这个。）
# 客户端两种，用 BK_CLIENT 选：
#   zenity（默认）—— GTK3，宣告 preedit 能力：拼音在**输入框内**（面板无拼音行）
#   xterm —— XIM，没有 preedit 能力：拼音只能在**面板顶部**（复现"关闭该开关"的样子）
CLIENT="${BK_CLIENT:-zenity}"
pkill -x zenity 2>/dev/null
pkill -x xterm 2>/dev/null
sleep 0.3
if [ "$CLIENT" = "xterm" ]; then
  LANG=C.UTF-8 LC_ALL=C.UTF-8 xterm -fa Monospace -fs 16 -geometry 90x12+40+40 >/dev/null 2>&1 &
  sleep 2
  WID=$(xdotool search --class xterm | tail -1)
else
  rm -rf "$HOME_DIR/.config/mousepad" "$HOME_DIR/.local/share/mousepad"
  zenity --entry --title=typing --text= >/dev/null 2>&1 &
  sleep 2
  WID=$(xdotool search --class zenity | tail -1)
fi
xdotool windowmove "$WID" 420 300 2>/dev/null
sleep 0.5
xdotool windowactivate --sync "$WID" 2>/dev/null || xdotool windowfocus "$WID"
sleep 0.5
xdotool type --window "$WID" --delay 150 "$TEXT"
sleep 2

import -window root "$OUT" 2>/dev/null || xwd -root | convert xwd:- "$OUT"

printf 'saved %s\n' "$OUT"
printf 'IM=%s\n' "$(fcitx5-remote -n 2>/dev/null)"
printf 'rime 已加载=%s\n' "$(grep -c 'Loaded addon rime' /tmp/fcitx5.log)"
printf 'UI 插件=%s\n' "$(grep -o 'Loaded addon [a-z]*ui' /tmp/fcitx5.log | head -1)"
printf '日志错误=%s\n' "$(grep -ciE '^E[0-9]' /tmp/fcitx5.log)"
