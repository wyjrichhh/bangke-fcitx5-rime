#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DEST="$1"
# Debian/Ubuntu use a multiarch library directory; Arch uses /usr/lib.
LIBDIR=$(pkg-config --variable=libdir Fcitx5Core)
[[ $LIBDIR == /* ]] || { echo 'Fcitx5 library directory unavailable' >&2; exit 1; }
mkdir -p "$DEST$LIBDIR/fcitx5" "$DEST/usr/share/fcitx5/addon"
c++ -shared -fPIC -std=c++20 -O2 $(pkg-config --cflags Fcitx5Core) \
  -I/usr/include/Fcitx5/Module "$HERE/bangke-tray.cpp" \
  $(pkg-config --libs Fcitx5Core) -o "$DEST$LIBDIR/fcitx5/bangke-tray.so"
install -m644 "$HERE/bangke-tray.conf" "$DEST/usr/share/fcitx5/addon/"
