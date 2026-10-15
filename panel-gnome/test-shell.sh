#!/usr/bin/env bash
# Launch a real GNOME compositor with a private bus, settings and runtime.
# No changes to the logged-in user's desktop or input-method configuration.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
RESULTS=${1:?Usage: test-shell.sh /absolute/results-directory}
mkdir -p "$RESULTS"
RESULTS=$(cd "$RESULTS" && pwd)
SANDBOX=$(mktemp -d)
cleanup() {
  for attempt in {1..10}; do
    rm -rf "$SANDBOX" 2>/dev/null && return 0
    sleep 0.1
  done
  echo "Test services are still closing; temporary directory retained: $SANDBOX" >&2
}
trap cleanup EXIT
mkdir -p "$SANDBOX/"{home,config,data/gnome-shell/extensions,cache,runtime}
chmod 700 "$SANDBOX/runtime"
cp -a "$REPO/panel-gnome/bangke-panel@bangke.org" "$SANDBOX/data/gnome-shell/extensions/"
export HOME="$SANDBOX/home" XDG_CONFIG_HOME="$SANDBOX/config" XDG_DATA_HOME="$SANDBOX/data"
export XDG_CACHE_HOME="$SANDBOX/cache" XDG_RUNTIME_DIR="$SANDBOX/runtime"
unset DBUS_SESSION_BUS_ADDRESS DISPLAY WAYLAND_DISPLAY GNOME_SHELL_SESSION_MODE
export XDG_CURRENT_DESKTOP=GNOME
export BANGKE_TEST_REPO="$REPO" BANGKE_TEST_RESULTS="$RESULTS"
dbus-run-session -- bash <<'RUN'
set -euo pipefail
gsettings set org.gnome.shell enabled-extensions "['bangke-panel@bangke.org']"
gsettings set org.gnome.shell welcome-dialog-last-shown-version '46.0'
gsettings set org.gnome.desktop.interface enable-animations false
gnome-shell --headless --wayland --no-x11 --virtual-monitor 1280x800 --sm-disable > "$BANGKE_TEST_RESULTS/shell.log" 2>&1 &
COMPOSITOR=$!
trap 'kill "$COMPOSITOR" 2>/dev/null || true; wait "$COMPOSITOR" 2>/dev/null || true' EXIT
for i in {1..100}; do
  if gdbus call --session --dest org.kde.impanel --object-path /org/kde/impanel \
      --method org.kde.impanel2.GetPanelIdentity 2>/dev/null; then break; fi
  kill -0 "$COMPOSITOR" || { cat "$BANGKE_TEST_RESULTS/shell.log"; exit 1; }
  sleep 0.2
done
python3 "$BANGKE_TEST_REPO/panel-gnome/test-shell.py" "$BANGKE_TEST_RESULTS"
if [[ ${BANGKE_TEST_INSTALLED_INPUT:-0} == 1 ]]; then
  export GTK_IM_MODULE=fcitx XMODIFIERS=@im=fcitx WAYLAND_DISPLAY=wayland-0
  python3 "$BANGKE_TEST_REPO/panel-gnome/test-input.py" "$BANGKE_TEST_RESULTS"
fi
RUN
