#!/usr/bin/env bash
set -euo pipefail
PRIV="${PRIV:-/priv}"
OUT="${OUT:-/out}"
VERSION="${VERSION:-5.0.22}"
PKG="$OUT/bangke-pkg"
MAIN_BASELINE="${BANGKE_MAIN_BASELINE:-origin/main}"
MAIN_COMMIT=$(git -C "$PRIV" rev-parse --verify "$MAIN_BASELINE^{commit}")
BUILD_COMMIT=$(git -C "$PRIV" rev-parse --verify HEAD)
git -C "$PRIV" merge-base --is-ancestor "$MAIN_COMMIT" "$BUILD_COMMIT" \
  || { echo "Refusing to package a branch that does not contain main: $MAIN_COMMIT" >&2; exit 1; }
printf 'Main baseline: %s\nPackaging commit: %s\nPlatform: %s\n' "$MAIN_COMMIT" "$BUILD_COMMIT" "$(uname -m)"
# The install step rewrites the build-tree RUNPATH to the private runtime directory.
ADDON="$PKG/usr/lib/fcitx5/rime-ai.so"
ADDON_RUNPATH=$(readelf -d "$ADDON" | awk '/\(RUNPATH\)|\(RPATH\)/ {sub(/.*\[/, ""); sub(/\].*/, ""); print}')
case ":$ADDON_RUNPATH:" in
  *":/usr/lib/fcitx5-rime-ai:"*) ;;
  *) echo "Refusing to package addon without its installed private-runtime RUNPATH: $ADDON_RUNPATH" >&2; exit 1 ;;
esac
# Refresh lifecycle/data files without recompiling the already built native binaries.
bash "$PRIV/dist/lifecycle/stage.sh" "$PKG" omarchy
rm -f "$PKG/usr/share/rime-data/fcitx5.yaml"
rmdir "$PKG/usr/share/rime-data" 2>/dev/null || true
install -m644 "$PRIV/data/fcitx5.yaml" "$PKG/usr/share/fcitx5-rime/seed/fcitx5.yaml"
python3 "$PRIV/dist/lifecycle/opencc-seed.py" "$PKG"
cp "$PRIV/dist/arch/PKGBUILD" "$PRIV/dist/arch/bangke-pinyin.install" "$OUT/pkgbuild/"
rm -rf "$OUT/pkgbuild/bangke-pkg"
cp -a "$PKG" "$OUT/pkgbuild/bangke-pkg"
if [ "$(id -u)" -eq 0 ]; then
  chown -R builder "$OUT/pkgbuild"
fi
cd "$OUT/pkgbuild"
sed -i "s/^pkgver=.*/pkgver=$VERSION/" PKGBUILD
if [ "$(id -u)" -eq 0 ]; then
  sudo -u builder env PKGEXT='.pkg.tar.zst' makepkg -f --noconfirm --nodeps
else
  PKGEXT='.pkg.tar.zst' makepkg -f --noconfirm --nodeps
fi
mv ./*.pkg.tar.zst "$OUT/"
