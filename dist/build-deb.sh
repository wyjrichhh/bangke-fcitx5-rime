#!/bin/bash
set -euo pipefail
STAGE="$HOME/bangke/openSource/deb-stage"
PREFIX="/usr/local"
VERSION="5.1.14"
ARCH="amd64"
PKG="fcitx5-rime-ai-predict"

rm -rf "$STAGE"; mkdir -p "$STAGE"

# 1) stage 已构建产物 (librime + 插件 + fcitx5-rime)
cd "$HOME/bangke/openSource/librime"
rm -f build/install_manifest.txt
DESTDIR="$STAGE" cmake --install build >/dev/null 2>&1
cd "$HOME/bangke/openSource/fcitx5-rime"
rm -f build/install_manifest.txt
DESTDIR="$STAGE" cmake --install build >/dev/null 2>&1

# 2) 模型 + 默认配置
D="$STAGE$PREFIX/share/fcitx5-rime"
mkdir -p "$D/predict_models" "$D/seed"
cp -a "$HOME/bangke/openSource/deb-resources/predict_models/zh-base-ct2-int8" "$D/predict_models/"
cp -a "$HOME/bangke/openSource/deb-resources/ai-predict-defaults/." "$D/seed/"
rm -f "$D/seed/squirrel.custom.yaml"
cp -a "$HOME/bangke/openSource/deb-resources/rime-ice" "$D/seed/rime-ice"

# 3) 注入 model_path 绝对路径
MODEL_PATH="$PREFIX/share/fcitx5-rime/predict_models/zh-base-ct2-int8"
for f in luna_pinyin.custom.yaml rime_ice.custom.yaml; do
  printf '  ai_predict:\n    model_path: %s\n' "$MODEL_PATH" >> "$D/seed/$f"
done

# 4) ld.so.conf
mkdir -p "$STAGE/etc/ld.so.conf.d"
echo "$PREFIX/lib" > "$STAGE/etc/ld.so.conf.d/fcitx5-rime-ai.conf"

# 5) DEBIAN 元数据
mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: Beijing Bangke Intelligent Technology Co., Ltd.
Depends: libboost-filesystem1.83.0, libboost-system1.83.0, libboost-regex1.83.0, libboost-locale1.83.0, libyaml-cpp0.7, libgoogle-glog0v6t64, libleveldb1d, libmarisa0, libopencc1.1, libgflags2.2
Description: Rime input method for Fcitx5 with AI prediction plugin
 Fcitx5 frontend for the Rime input method engine, bundled with the
 librime-ai-predict plugin (CTranslate2-based AI candidate prediction).
 Requires fcitx5 >= 5.1.22 (not in Ubuntu 24.04 repos; build from source).
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

# 6) 打包
cd "$HOME/bangke/openSource"
dpkg-deb --build --root-owner-group "$STAGE" "${PKG}_${VERSION}_${ARCH}.deb"
echo "=== 产出 ==="
ls -lh "${PKG}_${VERSION}_${ARCH}.deb"
