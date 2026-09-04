#!/bin/bash
set -euo pipefail
# 品牌名统一入口
[ -f /src/im-brand.env ] && . /src/im-brand.env
[ -f "$HOME"/bangke/openSource/im-brand.env ] && . "$HOME"/bangke/openSource/im-brand.env
IM_NAME="${IM_NAME:-蚌壳拼音}"
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cmake >/dev/null 2>&1
STAGE=/src/stage-final
PRIVLIB=/usr/lib/fcitx5-rime-ai
VERSION=5.0.11
ARCH=amd64
PKG=fcitx5-rime-ai-predict

rm -rf "$STAGE"; mkdir -p "$STAGE"

# 1) fcitx5-rime addon + 配置 → /usr
cd /src/fcitx5-rime-5.0.11
rm -f build/install_manifest.txt
DESTDIR="$STAGE" cmake --install build --prefix /usr >/dev/null 2>&1
mkdir -p "$STAGE/usr/lib/x86_64-linux-gnu/fcitx5"
if [ -f "$STAGE/usr/lib/fcitx5/rime.so" ]; then
  mv "$STAGE/usr/lib/fcitx5/rime.so" "$STAGE/usr/lib/x86_64-linux-gnu/fcitx5/rime.so"
  rmdir "$STAGE/usr/lib/fcitx5" 2>/dev/null || true
fi

# 1.5) config-ui (Qt5) → /usr
cd /src/config-ui
rm -f build/install_manifest.txt
DESTDIR="$STAGE" cmake --install build --prefix /usr >/dev/null 2>&1

# 2) librime + 插件 + 捆绑库 → 私有目录
mkdir -p "$STAGE$PRIVLIB/rime-plugins"
cp -a /src/librime/build/lib/librime.so* "$STAGE$PRIVLIB/"
cp -a /src/librime/build/lib/rime-plugins/librime-ai-predict.so "$STAGE$PRIVLIB/rime-plugins/"
cp -a /src/librime/plugins/librime-ai-predict/lib/libctranslate2.so* "$STAGE$PRIVLIB/" 2>/dev/null || true
cp -a /src/bundle-libs/*.so* "$STAGE$PRIVLIB/" 2>/dev/null || true
rm -f "$STAGE$PRIVLIB/librime.so.1" 2>/dev/null || true

# 3) 模型 + 默认配置 → /usr/share/fcitx5-rime/
D="$STAGE/usr/share/fcitx5-rime"
mkdir -p "$D/predict_models" "$D/seed"
cp -a /src/deb-resources/predict_models/zh-base-ct2-int8 "$D/predict_models/"
cp -a /src/deb-resources/ai-predict-defaults/*.custom.yaml "$D/seed/"
rm -f "$D/seed/squirrel.custom.yaml"
cp -a /src/deb-resources/ai-predict-defaults/dicts/. "$D/seed/"
cp -a /src/deb-resources/rime-ice/. "$D/seed/"

# 4) ld.so.conf → 私有目录
mkdir -p "$STAGE/etc/ld.so.conf.d"
echo "$PRIVLIB" > "$STAGE/etc/ld.so.conf.d/fcitx5-rime-ai.conf"


# 品牌名注入（改 im-brand.env 的 IM_NAME 后重跑即可全局改名）
for conf in "$STAGE/usr/share/fcitx5/inputmethod/rime.conf" \
            "$STAGE/usr/share/fcitx5/addon/rime.conf" \
            "$STAGE/usr/local/share/fcitx5/addon/rime.conf" \
            "$STAGE/usr/share/applications/fcitx5-rime-config.desktop"; do
  [ -f "$conf" ] && sed -i "s/^Name=.*/Name=$IM_NAME/" "$conf"
done
# 6) 内置中文字体 → /usr/share/fonts/
mkdir -p "$STAGE/usr/share/fonts/truetype/fcitx5-rime"
cp -a /src/deb-resources/fonts/*.ttf /src/deb-resources/fonts/*.otf "$STAGE/usr/share/fonts/truetype/fcitx5-rime/" 2>/dev/null || true

# 5) DEBIAN 元数据
mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: Beijing Bangke Intelligent Technology Co., Ltd.
Depends: fcitx5, libc6, libstdc++6, libgcc-s1, libqt5widgets5, libqt5dbus5
Description: Rime input method for Fcitx5 with AI prediction plugin
 Fcitx5 frontend for the Rime input method engine, bundled with the
 librime-ai-predict plugin (CTranslate2-based AI candidate prediction)
 and a Qt5 configuration UI.
EOF

cat > "$STAGE/DEBIAN/postinst" <<'PEOF'
#!/bin/sh
set -e
ldconfig || true
fc-cache -f >/dev/null 2>&1 || true
gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
SEED=/usr/share/fcitx5-rime/seed
R=".local/share/fcitx5/rime"
seed() { h="$1"; [ -d "$h" ] || return 0; t="$h/$R"; mkdir -p "$t"; cp -a "$SEED"/. "$t"/; chown -R "$(stat -c '%U:%G' "$h")" "$t" 2>/dev/null || true; }
seed /etc/skel
[ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ] && seed "$(getent passwd "$SUDO_USER" | cut -d: -f6)"
exit 0
PEOF
chmod 0755 "$STAGE/DEBIAN/postinst"

cat > "$STAGE/DEBIAN/prerm" <<'PEOF'
#!/bin/sh
exit 0
PEOF
chmod 0755 "$STAGE/DEBIAN/prerm"

cat > "$STAGE/DEBIAN/postrm" <<'PEOF'
#!/bin/sh
set -e
ldconfig || true
exit 0
PEOF
chmod 0755 "$STAGE/DEBIAN/postrm"

# 6) 打包
cd /src
dpkg-deb --build --root-owner-group "$STAGE" "${PKG}_${VERSION}_${ARCH}.deb"
echo "=== 产出 ==="
ls -lh "${PKG}_${VERSION}_${ARCH}.deb"
echo "=== 关键文件 ==="
dpkg-deb -c "${PKG}_${VERSION}_${ARCH}.deb" 2>/dev/null | grep -E "fcitx5/rime\.so$|librime\.so\.1\.16|ai-predict\.so|ld\.so\.conf|model\.bin$|fcitx5-rime-config"
