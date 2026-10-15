#!/usr/bin/env bash
set -euo pipefail
ROOT="$1"
PLATFORM="$2"
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
mkdir -p "$ROOT/usr/lib/bangke-pinyin" "$ROOT/etc/xdg/autostart" "$ROOT/usr/share/bangke-pinyin"
install -m755 "$HERE/"{package.sh,session.sh,user-data.py,migrate-experiment.py} "$ROOT/usr/lib/bangke-pinyin/"
cat > "$ROOT/etc/xdg/autostart/bangke-pinyin-setup.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Bangke Pinyin initialization
Exec=/usr/lib/bangke-pinyin/session.sh install
NoDisplay=true
EOF
mkdir -p "$ROOT/usr/lib/systemd/user" "$ROOT/etc/systemd/user/graphical-session.target.wants"
ln -sfn /usr/lib/systemd/user/bangke-update-monitor.service "$ROOT/etc/systemd/user/graphical-session.target.wants/bangke-update-monitor.service"
cat > "$ROOT/usr/lib/systemd/user/bangke-update-monitor.service" <<'EOF'
[Unit]
Description=Bangke software update notifications
PartOf=graphical-session.target
After=graphical-session.target
[Service]
ExecStart=/usr/bin/fcitx5-rime-config --update-monitor
Restart=on-failure
RestartSec=60
EOF
touch "$ROOT/usr/share/bangke-pinyin/$PLATFORM"
if [[ $PLATFORM == omarchy ]]; then
  touch "$ROOT/usr/share/bangke-pinyin/omarchy"
  rm -rf "$ROOT/usr/share/bangke-pinyin/omarchy-themes"
  cp -a "$REPO/panel-omarchy/themes" "$ROOT/usr/share/bangke-pinyin/omarchy-themes"
  bash "$REPO/panel-omarchy/tray/build.sh" "$ROOT"
  install -m755 "$REPO/panel-omarchy/panel.py" "$ROOT/usr/bin/bangke-panel"
  install -m644 "$REPO/panel-omarchy/hyprland-caret-plugin/caretshim.cpp" "$ROOT/usr/share/bangke-pinyin/"
  install -m755 "$HERE/caret.sh" "$ROOT/usr/lib/bangke-pinyin/"
  # Omarchy starts session services through systemd; do not rely on XDG autostart.
  rm -f "$ROOT/etc/xdg/autostart/bangke-pinyin-setup.desktop"
  mkdir -p "$ROOT/usr/lib/systemd/user" "$ROOT/etc/systemd/user/graphical-session.target.wants"
  ln -sfn /usr/lib/systemd/user/bangke-session.service "$ROOT/etc/systemd/user/graphical-session.target.wants/bangke-session.service"
  cat > "$ROOT/usr/lib/systemd/user/bangke-session.service" <<'EOF'
[Unit]
Description=Initialize Bangke input method for this desktop session
PartOf=graphical-session.target
After=graphical-session.target
ConditionEnvironment=WAYLAND_DISPLAY
[Service]
Type=oneshot
ExecStart=/usr/lib/bangke-pinyin/session.sh install
EOF
  cat > "$ROOT/usr/lib/systemd/user/bangke-panel.service" <<'EOF'
[Unit]
Description=Bangke candidate panel
PartOf=graphical-session.target
After=graphical-session.target
[Service]
ExecStart=/usr/bin/bangke-panel
Restart=on-failure
RestartSec=2
EOF
  cat > "$ROOT/usr/lib/systemd/user/bangke-caret.service" <<'EOF'
[Unit]
Description=Bangke Hyprland caret integration
PartOf=graphical-session.target
After=graphical-session.target
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/lib/bangke-pinyin/caret.sh
ExecStop=/usr/lib/bangke-pinyin/caret.sh stop
EOF
  cat > "$ROOT/usr/share/bangke-pinyin/20-bangke-kimpanel.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=/usr/bin/fcitx5 --disable classicui --enable notificationitem,bangke-tray -u kimpanel
EOF
fi
