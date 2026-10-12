#!/bin/bash
# 蚌壳拼音（Ubuntu 版）卸载 / 重装脚本。在构建机上跑：
#
#   bash ~/bangke/bk-reinstall.sh                  # 卸载后重装（保留用户配置与词典）
#   bash ~/bangke/bk-reinstall.sh --purge-config   # 连用户配置一起清掉（**会丢用户词典/自造词**）
#   bash ~/bangke/bk-reinstall.sh --uninstall-only # 只卸载，不装
#
# 需要免密 sudo。装完会尽量帮你重启输入法；重启不了会告诉你怎么手动做。
set -uo pipefail   # 故意不用 -e：每步自己判断，别因为一步失败就半途停下

DEB_DIR="$HOME/bangke/commit/fcitx5-rime/build-out"
PKG=fcitx5-rime-ai-predict
USER_RIME="$HOME/.local/share/fcitx5/rime"
PURGE_CONFIG=0
UNINSTALL_ONLY=0

for arg in "$@"; do
  case "$arg" in
    --purge-config)   PURGE_CONFIG=1 ;;
    --uninstall-only) UNINSTALL_ONLY=1 ;;
    -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
    *) echo "未知参数：$arg（用 --help 看用法）"; exit 2 ;;
  esac
done

step() { printf '\n== %s ==\n' "$*"; }
warn() { printf '!! %s\n' "$*" >&2; }

if ! sudo -n true 2>/dev/null; then
  warn "需要免密 sudo（sudo -n true 失败）。请先配置，或以 root 运行。"
  exit 1
fi

step "1/6 卸载旧包"
if dpkg -l "$PKG" 2>/dev/null | grep -q '^ii'; then
  sudo dpkg -P "$PKG" || warn "dpkg -P 返回非零，继续清理残留"
else
  echo "（未安装，跳过）"
fi

step "2/6 清理残留文件"
sudo rm -rf /usr/lib/fcitx5-rime-ai \
            /usr/share/fcitx5-rime \
            /usr/local/share/fcitx5-rime \
            /etc/ld.so.conf.d/fcitx5-rime-ai.conf
sudo rm -f  /usr/lib/x86_64-linux-gnu/fcitx5/rime-ai.so \
            /usr/local/lib/fcitx5/rime-ai.so \
            /usr/bin/fcitx5-rime-config \
            /usr/share/applications/fcitx5-rime-config.desktop
sudo ldconfig
echo "残留清理完成"

if [ "$PURGE_CONFIG" = 1 ]; then
  step "2.5 清理用户配置（--purge-config）"
  echo "将删除：$USER_RIME 与 ~/.local/share/fcitx5/themes/BangKe*、~/.config/fcitx5/conf/classicui.conf"
  echo "**用户词典与自造词会一起消失**"
  rm -rf "$USER_RIME" \
         "$HOME/.local/share/fcitx5/themes/BangKe" \
         "$HOME/.local/share/fcitx5/themes/BangKe-Dark"
  rm -f  "$HOME/.config/fcitx5/conf/classicui.conf"
  echo "用户配置已清空"
fi

if [ "$UNINSTALL_ONLY" = 1 ]; then
  step "完成（只卸载）"
  echo "重启输入法后生效：注销重登，或在你桌面上的终端里执行 fcitx5 -r"
  exit 0
fi

step "3/6 找安装包"
DEB=$(ls -t "$DEB_DIR"/${PKG}_*_amd64.deb 2>/dev/null | head -1)
if [ -z "${DEB:-}" ]; then
  warn "在 $DEB_DIR 下找不到 ${PKG}_*_amd64.deb"
  exit 1
fi
echo "$DEB"

step "4/6 安装"
if ! sudo dpkg -i "$DEB"; then
  warn "dpkg 报错，尝试补齐依赖后重试"
  sudo apt-get -f install -y || true
  sudo dpkg -i "$DEB" || { warn "安装仍然失败，见上面的报错"; exit 1; }
fi

step "5/6 校验"
ok=0; bad=0
check() { if [ -e "$2" ]; then echo "  OK   $1"; ok=$((ok+1)); else echo "  缺   $1  ($2)"; bad=$((bad+1)); fi; }
check "候选面板主题（用户）" "$HOME/.local/share/fcitx5/themes/BangKe/theme.conf"
check "classicui 指向我们的主题" "$HOME/.config/fcitx5/conf/classicui.conf"
check "配色数据（用户 rime 目录）" "$USER_RIME/bangke.yaml"
check "设置程序" "/usr/bin/fcitx5-rime-config"
check "rime addon" "/usr/lib/x86_64-linux-gnu/fcitx5/rime-ai.so"
check "AI 插件" "/usr/lib/fcitx5-rime-ai/rime-plugins/librime-ai-predict.so"
check "AI 模型" "/usr/share/fcitx5-rime/predict_models/zh-base-ct2-int8/model.bin"
missing=$(ldd /usr/lib/fcitx5-rime-ai/rime-plugins/librime-ai-predict.so 2>/dev/null | grep -c 'not found')
[ "$missing" = "0" ] && echo "  OK   AI 插件依赖齐全" || echo "  警告 AI 插件有 $missing 个依赖找不到"
echo "已装版本：$(dpkg-query -W -f='${Version}' "$PKG" 2>/dev/null || echo '?')"
echo "校验：$ok 项在位，$bad 项缺失"

step "6/6 重启输入法"
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
if [ -S "$XDG_RUNTIME_DIR/bus" ]; then
  export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
fi
if systemctl --user list-unit-files 2>/dev/null | grep -q '^fcitx5'; then
  systemctl --user restart fcitx5 && echo "已通过 systemd 用户服务重启 fcitx5"
elif [ -n "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]; then
  fcitx5 -r >/dev/null 2>&1 && echo "已执行 fcitx5 -r（若没生效，注销重登一次）"
else
  echo "看不到图形会话 —— 请在桌面上的终端里执行 fcitx5 -r，或注销重登一次。"
fi

cat <<'TIP'

== 接下来你会看到什么 ==
1. 未登录前不用管；登录后打字应出现带圆角的候选面板（主题在用户目录里）。
2. 设置程序：应用列表里搜「Rime 配置」，或命令行 fcitx5-rime-config。
   它的外观页只放 fcitx5 classicui 真能生效的项。
3. AI 预测默认要打满 12 个字符才触发（min_input_length），
   嫌门槛高就在「智能联想」页把「最小输入长度」调小；打完停一下再看候选。
TIP
