#!/usr/bin/env bash
# Runs as the desktop user, never as root. Serialize install and login initialization.
set -euo pipefail
[[ $(id -u) != 0 ]] || exit 0
ACTION="${1:-install}"
RUNTIME="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/bangke-pinyin"
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}"
# User systemd keeps the compositor environment even when pacman runs from sudo.
while IFS='=' read -r name value; do
  case "$name" in
    WAYLAND_DISPLAY|DISPLAY|HYPRLAND_INSTANCE_SIGNATURE|XDG_CURRENT_DESKTOP|XDG_SESSION_TYPE)
      [[ -n ${!name:-} ]] || export "$name=$value" ;;
  esac
done < <(systemctl --user show-environment 2>/dev/null || true)
mkdir -p "$CACHE"
exec 9> "$CACHE/session.lock"
flock -x 9
exec >> "$CACHE/setup.log" 2>&1
echo "$(date -Is) $ACTION"
BASE="${BANGKE_LIFECYCLE_DIR:-/usr/lib/bangke-pinyin}"
UNIT=omarchy-fcitx5.service
MANAGED=0
RUNNING=0
if systemctl --user is-active --quiet "$UNIT"; then
  MANAGED=1; RUNNING=1
  systemctl --user stop "$UNIT"
else
  # Only stop this user's processes, never every fcitx5 instance on the machine.
  mapfile -t PIDS < <(pgrep -u "$(id -u)" -x fcitx5 || true)
  if ((${#PIDS[@]})); then
    RUNNING=1
    kill -TERM "${PIDS[@]}" 2>/dev/null || true
    for i in {1..30}; do
      alive=0
      for pid in "${PIDS[@]}"; do
        [[ -r /proc/$pid/stat ]] && [[ $(awk '{print $3}' /proc/"$pid"/stat) != Z ]] && alive=1
      done
      [[ $alive == 0 ]] && break
      sleep 0.1
    done
    [[ $alive == 0 ]] || { echo 'Fcitx5 did not exit; leaving its profile unchanged'; exit 1; }
  fi
fi
restart() {
  systemctl --user daemon-reload || true
  if [[ $MANAGED == 1 ]]; then
    systemctl --user start "$UNIT" || true
  elif [[ $RUNNING == 1 ]]; then
    fcitx5 -d 9>&- >/dev/null 2>&1 &
  fi
}
trap restart EXIT
EXTRA=()
[[ ! -f /usr/share/bangke-pinyin/ubuntu && ! -f "$BASE/ubuntu" ]] || EXTRA=(--ubuntu)
python3 "$BASE/user-data.py" "$ACTION" "${EXTRA[@]}"
if [[ -f /usr/share/bangke-pinyin/omarchy || -f "$BASE/omarchy" ]]; then
  if [[ $ACTION == install ]]; then
    # Migrate only the exact experimental launch paths; retain unrelated Hyprland configuration.
    python3 "$BASE/migrate-experiment.py"
    systemctl --user daemon-reload || true
    if [[ -n ${HYPRLAND_INSTANCE_SIGNATURE:-} ]]; then
      systemctl --user import-environment WAYLAND_DISPLAY HYPRLAND_INSTANCE_SIGNATURE XDG_CURRENT_DESKTOP || true
      systemctl --user restart bangke-caret.service || true
      if systemctl --user restart bangke-panel.service &&
         timeout 8 bash -c 'until gdbus call --session --dest org.kde.impanel --object-path /org/kde/impanel --method org.kde.impanel2.GetPanelIdentity; do sleep 0.2; done'; then
        mkdir -p "$CONFIG/systemd/user/$UNIT.d"
        cp /usr/share/bangke-pinyin/20-bangke-kimpanel.conf "$CONFIG/systemd/user/$UNIT.d/20-bangke-kimpanel.conf"
      else
        rm -f "$CONFIG/systemd/user/$UNIT.d/20-bangke-kimpanel.conf"
        echo 'Panel unavailable; keeping classic UI'
      fi
    fi
  else
    systemctl --user stop bangke-panel.service bangke-caret.service || true
    rm -f "$CONFIG/systemd/user/$UNIT.d/20-bangke-kimpanel.conf"
    python3 "$BASE/migrate-experiment.py" --remove
  fi
fi
