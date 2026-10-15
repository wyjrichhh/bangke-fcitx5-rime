#!/usr/bin/env bash
# The desktop user runs this after seed/profile setup, before Fcitx restarts.
# GNOME must load newly installed system extensions at the next login.
set -euo pipefail
UUID=bangke-panel@bangke.org
STATE="${XDG_DATA_HOME:-$HOME/.local/share}/bangke-pinyin/gnome-panel-enabled"
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/bangke-panel"
if [[ ${1:-install} == remove ]]; then
  if [[ -f $STATE ]]; then
    gnome-extensions disable "$UUID" || true
    rm -f "$STATE"
  fi
  exit 0
fi
case ":${XDG_CURRENT_DESKTOP,,}:" in *:gnome:*|*:ubuntu:*) ;; *) exit 0 ;; esac
command -v gnome-extensions >/dev/null || exit 0
# A different panel owns this name: don't replace it or disable the user's extension.
OWNER=$(gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
  --method org.freedesktop.DBus.NameHasOwner org.kde.impanel 2>/dev/null || true)
if [[ $OWNER == *true* ]]; then
  IDENTITY=$(gdbus call --session --dest org.kde.impanel --object-path /org/kde/impanel \
    --method org.kde.impanel2.GetPanelIdentity 2>/dev/null || true)
  [[ $IDENTITY == *bangke-panel:gnome-* ]] || { echo 'Existing Kimpanel retained'; exit 0; }
fi
# Generate through the same Qt/YAML parser used by Settings and the Hyprland panel.
# Regenerate at login so YAML edits made outside Settings also take effect.
mkdir -p "$CONFIG"
TEMP=$(mktemp "$CONFIG/.appearance.XXXXXX")
trap 'rm -f "$TEMP"' EXIT
if BANGKE_PANEL_BACKEND=gnome QT_QPA_PLATFORM=offscreen fcitx5-rime-config --dump-panel-appearance > "$TEMP"; then
  python3 - "$TEMP" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    value = json.load(f)
assert value['version'] == 1 and isinstance(value['style'], dict)
PY
  mv "$TEMP" "$CONFIG/appearance.json"
else
  echo 'Appearance generation failed; previous configuration retained'
fi
if ! gnome-extensions info "$UUID" >/dev/null 2>&1; then
  echo 'Bangke GNOME extension installed; log out and back in to load it'
  exit 0
fi
# Record only enablement performed by us, so removal preserves a user's choice.
if ! gnome-extensions list --enabled | grep -qxF "$UUID"; then
  if gnome-extensions enable "$UUID"; then
    mkdir -p "$(dirname "$STATE")"
    touch "$STATE"
  else
    echo 'GNOME extension unavailable; keeping Classic UI'
    exit 0
  fi
fi
if ! timeout 5 bash -c 'until gdbus call --session --dest org.kde.impanel --object-path /org/kde/impanel --method org.kde.impanel2.GetPanelIdentity 2>/dev/null | grep -q "bangke-panel:gnome-"; do sleep 0.2; done'; then
  echo 'GNOME panel did not acquire Kimpanel; keeping existing Fcitx UI'
fi
