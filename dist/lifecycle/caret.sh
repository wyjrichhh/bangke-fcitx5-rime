#!/usr/bin/env bash
# Use a new pathname for each build: dlclose may retain GNU-unique C++ objects.
set -euo pipefail
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/bangke-pinyin/caret"
mkdir -p "$CACHE"
ACTIVE="$CACHE/active-path"
PREVIOUS="$CACHE/bangke-im-caret.so"
[[ ! -f $ACTIVE ]] || PREVIOUS="$(cat "$ACTIVE")"
if [[ ${1:-} == stop ]]; then
  hyprctl plugin unload "$PREVIOUS" || true
  exit 0
fi
SOURCE=/usr/share/bangke-pinyin/caretshim.cpp
pkg-config --exists hyprland
KEY="$(pkg-config --modversion hyprland)-$(sha256sum "$SOURCE" /usr/include/hyprland/src/version.h | sha256sum | cut -d' ' -f1)"
SO="$CACHE/bangke-im-caret-$KEY.so"
if [[ ! -f $SO ]]; then
  g++ -shared -fPIC -fno-gnu-unique -std=c++23 -O2 $(pkg-config --cflags hyprland) "$SOURCE" -o "$SO.next"
  mv "$SO.next" "$SO"
fi
hyprctl plugin unload "$PREVIOUS" >/dev/null 2>&1 || true
if [[ $SO != "$PREVIOUS" ]]; then hyprctl plugin unload "$SO" >/dev/null 2>&1 || true; fi
hyprctl plugin load "$SO"
printf '%s\n' "$SO" > "$ACTIVE"
hyprctl bangkeimidentity | python3 -c 'import json,sys;x=json.load(sys.stdin);assert x.get("ok") and x.get("api")==3 and x.get("stable") and x.get("blur");print("Bangke caret API 3 verified")'
