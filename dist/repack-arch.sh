#!/usr/bin/env bash
set -euo pipefail
PRIV="${PRIV:-/priv}"
OUT="${OUT:-/out}"
VERSION="${VERSION:-5.0.19}"
PKG="$OUT/bangke-pkg"
# Refresh lifecycle/data files without recompiling the already built native binaries.
bash "$PRIV/dist/lifecycle/stage.sh" "$PKG" omarchy
rm -f "$PKG/usr/share/rime-data/fcitx5.yaml"
rmdir "$PKG/usr/share/rime-data" 2>/dev/null || true
install -m644 "$PRIV/data/fcitx5.yaml" "$PKG/usr/share/fcitx5-rime/seed/fcitx5.yaml"
python3 "$PRIV/dist/lifecycle/opencc-seed.py" "$PKG"
cp "$PRIV/dist/arch/PKGBUILD" "$PRIV/dist/arch/bangke-pinyin.install" "$OUT/pkgbuild/"
rm -rf "$OUT/pkgbuild/bangke-pkg"
cp -a "$PKG" "$OUT/pkgbuild/bangke-pkg"
chown -R builder "$OUT/pkgbuild"
cd "$OUT/pkgbuild"
sed -i "s/^pkgver=.*/pkgver=$VERSION/" PKGBUILD
sudo -u builder env PKGEXT='.pkg.tar.zst' makepkg -f --noconfirm --nodeps
mv ./*.pkg.tar.zst "$OUT/"
