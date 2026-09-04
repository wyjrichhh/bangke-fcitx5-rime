#!/bin/bash
set -euo pipefail
STAGE="$HOME/bangke/openSource/stage22"
PREFIX="/usr/local"
VERSION="5.0.11"
ARCH="amd64"
PKG="fcitx5-rime-ai-predict"

# 模型 + 默认配置
D="$STAGE$PREFIX/share/fcitx5-rime"
mkdir -p "$D/predict_models" "$D/seed"
cp -a "$HOME/bangke/openSource/deb-resources/predict_models/zh-base-ct2-int8" "$D/predict_models/"
cp -a "$HOME/bangke/openSource/deb-resources/ai-predict-defaults/." "$D/seed/"
rm -f "$D/seed/squirrel.custom.yaml"
cp -a "$HOME/bangke/openSource/deb-resources/rime-ice" "$D/seed/rime-ice"

# model_path 绝对路径
MODEL_PATH="$PREFIX/share/fcitx5-rime/predict_models/zh-base-ct2-int8"
for f in luna_pinyin.custom.yaml rime_ice.custom.yaml; do
  printf '  ai_predict:\n    model_path: %s\n' "$MODEL_PATH" >> "$D/seed/$f"
done

# ld.so.conf
mkdir -p "$STAGE/etc/ld.so.conf.d"
echo "$PREFIX/lib" > "$STAGE/etc/ld.so.conf.d/fcitx5-rime-ai.conf"

# DEBIAN
mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: Beijing Bangke Intelligent Technology Co., Ltd.
Depends: fcitx5, libc6, libstdc++6, libgcc-s1
Description: Rime input method for Fcitx5 with AI prediction plugin
 Fcitx5 frontend for the Rime input method engine, bundled with the
 librime-ai-predict plugin (CTranslate2-based AI candidate prediction).
EOF

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
ldconfig || true
SEED=/usr/local/share/fcitx5-rime/seed
R=".local/share/fcitx5/rime"
seed() { h="$1"; [ -d "$h" ] || return 0; t="$h/$R"; mkdir -p "$t"; cp -a "$SEED"/. "$t"/; chown -R "$(stat -c '%U:%G' "$h")" "$t" 2>/dev/null || true; }
seed /etc/skel
[ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ] && seed "$(getent passwd "$SUDO_USER" | cut -d: -f6)"
exit 0
EOF
chmod 0755 "$STAGE/DEBIAN/postinst"

cat > "$STAGE/DEBIAN/prerm" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod 0755 "$STAGE/DEBIAN/prerm"

cd "$HOME/bangke/openSource"
dpkg-deb --build --root-owner-group "$STAGE" "${PKG}_${VERSION}_${ARCH}.deb"
ls -lh "${PKG}_${VERSION}_${ARCH}.deb"
