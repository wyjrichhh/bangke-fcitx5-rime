#!/bin/bash
set -euo pipefail
# 品牌名统一入口
[ -f /src/im-brand.env ] && . /src/im-brand.env
[ -f "$HOME"/bangke/openSource/im-brand.env ] && . "$HOME"/bangke/openSource/im-brand.env
IM_NAME="${IM_NAME:-蚌壳拼音}"
STAGE="$HOME/bangke/openSource/stage-final"
PRIVLIB="/usr/lib/fcitx5-rime-ai"
VERSION="5.0.11"
ARCH="amd64"
PKG="fcitx5-rime-ai-predict"

rm -rf "$STAGE"; mkdir -p "$STAGE"

# 1) fcitx5-rime addon + 配置 → /usr 标准位置
cd "$HOME/bangke/openSource/fcitx5-rime-5.0.11"
rm -f build/install_manifest.txt
DESTDIR="$STAGE" cmake --install build --prefix /usr >/dev/null 2>&1
# 把 rime.so 挪到 fcitx5 实际搜索的 multiarch 目录
mkdir -p "$STAGE/usr/lib/x86_64-linux-gnu/fcitx5"
if [ -f "$STAGE/usr/lib/fcitx5/rime.so" ]; then
  mv "$STAGE/usr/lib/fcitx5/rime.so" "$STAGE/usr/lib/x86_64-linux-gnu/fcitx5/rime.so"
  rmdir "$STAGE/usr/lib/fcitx5" 2>/dev/null || true
fi

# 1.5) config-ui (Qt5) → /usr
cd "$HOME/bangke/openSource/config-ui"
rm -f build/install_manifest.txt
DESTDIR="$STAGE" cmake --install build --prefix /usr >/dev/null 2>&1

# 2) librime 1.16.1 + 插件 + 捆绑库 → 私有目录
mkdir -p "$STAGE$PRIVLIB/rime-plugins"
cp -a "$HOME/bangke/openSource/librime/build/lib/librime.so"* "$STAGE$PRIVLIB/"
cp -a "$HOME/bangke/openSource/librime/build/lib/rime-plugins/librime-ai-predict.so" "$STAGE$PRIVLIB/rime-plugins/"
cp -a "$HOME/bangke/openSource/bundle-libs/"*.so* "$STAGE$PRIVLIB/" 2>/dev/null || true

# 3) 模型 + 默认配置 → /usr/share/fcitx5-rime/
D="$STAGE/usr/share/fcitx5-rime"
mkdir -p "$D/predict_models" "$D/seed"
cp -a "$HOME/bangke/openSource/deb-resources/predict_models/zh-base-ct2-int8" "$D/predict_models/"
cp -a "$HOME/bangke/openSource/deb-resources/ai-predict-defaults/." "$D/seed/"
rm -f "$D/seed/squirrel.custom.yaml"
cp -a "$HOME/bangke/openSource/deb-resources/rime-ice" "$D/seed/rime-ice"

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
cp -a "$HOME"/bangke/openSource/deb-resources/fonts/*.ttf "$HOME"/bangke/openSource/deb-resources/fonts/*.otf "$STAGE/usr/share/fonts/truetype/fcitx5-rime/" 2>/dev/null || true

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

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
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
echo "=== 关键文件位置 ==="
dpkg-deb -c "${PKG}_${VERSION}_${ARCH}.deb" 2>/dev/null | grep -E "rime\.so$|librime\.so\.1\.16|ai-predict\.so|ld\.so\.conf|model\.bin$|fcitx5-rime-config" | head
