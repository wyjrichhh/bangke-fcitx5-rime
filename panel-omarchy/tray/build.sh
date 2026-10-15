#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DEST="$1"
mkdir -p "$DEST/usr/lib/fcitx5" "$DEST/usr/share/fcitx5/addon"
c++ -shared -fPIC -std=c++20 -O2 $(pkg-config --cflags Fcitx5Core) \
  -I/usr/include/Fcitx5/Module "$HERE/bangke-tray.cpp" \
  $(pkg-config --libs Fcitx5Core) -o "$DEST/usr/lib/fcitx5/bangke-tray.so"
install -m644 "$HERE/bangke-tray.conf" "$DEST/usr/share/fcitx5/addon/"
