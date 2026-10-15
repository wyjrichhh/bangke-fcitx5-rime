#!/usr/bin/env bash
# Shared root package hook. Keep a copy for Debian postrm after package files disappear.
set -u
ACTION="${1:-install}"
STATE=/var/lib/bangke-pinyin
BASE=/usr/lib/bangke-pinyin
if [[ $ACTION == install ]]; then
  mkdir -p "$STATE/lifecycle"
  cp "$BASE/"{package.sh,session.sh,user-data.py,migrate-experiment.py} "$STATE/lifecycle/"
  for platform in ubuntu omarchy; do
    [[ ! -f /usr/share/bangke-pinyin/$platform ]] || touch "$STATE/lifecycle/$platform"
  done
  # Import legacy records so uninstall also cleans registrations from older installers.
  if [[ -f $STATE/registered-profile ]]; then
    while IFS= read -r old_home; do
      old_user=$(getent passwd | awk -F: -v home="$old_home" '$6 == home && $3 != 0 {print $1}')
      [[ -z $old_user ]] || { grep -qxF "$old_user" "$STATE/users" 2>/dev/null || echo "$old_user" >> "$STATE/users"; }
    done < "$STATE/registered-profile"
  fi
else
  BASE="$STATE/lifecycle"
fi
ldconfig || true
fc-cache -f >/dev/null 2>&1 || true
update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
# SUDO_USER may be absent for GUI package managers. Login autostart handles those users.
users="${SUDO_USER:-}"
# Login initialization also covers users who did not invoke the package manager.
# Discover their standard XDG manifests for symmetrical removal.
if [[ $ACTION == remove ]]; then
  while IFS=: read -r candidate _ uid _ _ candidate_home _; do
    [[ $uid != 0 && -f "$candidate_home/.local/share/bangke-pinyin/seed-manifest.json" ]] && users="$users $candidate"
  done < <(getent passwd)
fi
if [[ $ACTION == remove && -f $STATE/users ]]; then users="$(cat "$STATE/users") $users"; fi
for user in $(printf '%s\n' $users | sort -u); do
  [[ $user != root ]] && id "$user" >/dev/null 2>&1 || continue
  uid=$(id -u "$user")
  runtime=/run/user/$uid
  if [[ $ACTION == install ]]; then
    grep -qxF "$user" "$STATE/users" 2>/dev/null || echo "$user" >> "$STATE/users"
  fi
  # No compositor means only seed/profile work; do not manufacture a desktop session.
  runuser -u "$user" -- env XDG_RUNTIME_DIR="$runtime" DBUS_SESSION_BUS_ADDRESS="unix:path=$runtime/bus" \
    BANGKE_LIFECYCLE_DIR="$BASE" bash "$BASE/session.sh" "$ACTION" || echo "Bangke setup deferred for $user; see ~/.cache/bangke-pinyin/setup.log"
done
[[ $ACTION != remove ]] || rm -rf "$STATE"
exit 0
