#!/bin/bash
# 把已构建好的产物打成 deb（不在这里编译）。必须先跑过 build-22.04.sh：
#
#   docker run --rm \
#     -v ~/bangke/commit/fcitx5-rime:/priv \
#     -v ~/bangke/commit/bangke-librime:/librime:ro \
#     -v ~/bangke/commit/bangke-ai-predict:/plugin:ro \
#     -v ~/bangke/vendor:/vendor:ro \
#     -v ~/bangke/build-out/22.04:/out \
#     ubuntu:22.04 bash /priv/dist/repack-container.sh
#
# 来源（与 build-22.04.sh 同一套约定，全部来自 bangke 侧）：
#   /priv   私有库（addon、config-ui、data/bangke.yaml、data/ai-predict-defaults）
#   /out    上一阶段编出来的 librime / 插件 / CTranslate2
#   /vendor 暂存区（模型、字体、雾凇、词库、捆绑库）
set -euo pipefail

# 容器是干净基础镜像，cmake 只用来跑 --install（不在这里编译）
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cmake >/dev/null 2>&1

# 品牌名统一入口（只影响产物文件名，见 im-brand.env 的说明）
[ -f /priv/dist/im-brand.env ] && . /priv/dist/im-brand.env
IM_NAME="${IM_NAME:-蚌壳拼音}"

VERSION="${VERSION:-5.0.19}"
ARCH=amd64
PKG=fcitx5-rime-ai-predict
PRIVLIB=/usr/lib/fcitx5-rime-ai
OUT=/priv/build-out
STAGE="$OUT/stage-final"

# 打包前必须确认私有部分真的重编过（历史上出过「包里是上一次的设置程序」）。
if [ ! -f /priv/build/src/rime-ai.so ] && [ ! -f /priv/build/rime-ai.so ]; then
  echo "!! 找不到 /priv/build 里的 rime-ai.so，先跑 build-22.04.sh" >&2
  exit 1
fi
if [ ! -x /priv/config-ui/build/fcitx5-rime-config ]; then
  echo "!! 找不到 /priv/config-ui/build/fcitx5-rime-config，先跑 build-22.04.sh" >&2
  exit 1
fi

rm -rf "$STAGE"; mkdir -p "$STAGE"

# 1) fcitx5-rime addon + 配置 → /usr
cd /priv
rm -f build/install_manifest.txt
DESTDIR="$STAGE" cmake --install build --prefix /usr >/dev/null 2>&1
# Also handle a previously configured CMake build with the historical shared destination.
if [ -f "$STAGE/usr/share/rime-data/fcitx5.yaml" ]; then
  mkdir -p "$STAGE/usr/share/fcitx5-rime/seed"
  mv "$STAGE/usr/share/rime-data/fcitx5.yaml" "$STAGE/usr/share/fcitx5-rime/seed/"
  rmdir "$STAGE/usr/share/rime-data" 2>/dev/null || true
fi
# 把 rime-ai.so 挪到 fcitx5 实际搜索的 multiarch 目录
mkdir -p "$STAGE/usr/lib/x86_64-linux-gnu/fcitx5"
if [ -f "$STAGE/usr/lib/fcitx5/rime-ai.so" ]; then
  mv "$STAGE/usr/lib/fcitx5/rime-ai.so" "$STAGE/usr/lib/x86_64-linux-gnu/fcitx5/rime-ai.so"
  rmdir "$STAGE/usr/lib/fcitx5" 2>/dev/null || true
fi
# /priv/build 与 Arch 线共用，可能残留 Arch 编的 addon —— 它链的是 fcitx5 master 才有的
# registerDomain(path) 重载，在 22.04/24.04 的 fcitx5 上 dlopen 失败、fcitx5 开机即退。
# 先把 nm 的输出收完再比：`nm | grep -q` 会在命中时让 nm 吃 SIGPIPE，配合 pipefail 反而误判成失败。
RIME_SYMS=$(nm -D "$STAGE/usr/lib/x86_64-linux-gnu/fcitx5/rime-ai.so" || true)
case "$RIME_SYMS" in
  *registerDomainEPKcS1_*) ;;
  *)
    echo "!! rime-ai.so 不是对着 22.04/24.04 的 fcitx5 编的（缺 registerDomain(char const*, char const*)）" >&2
    echo "!! 先在 ubuntu:22.04 容器里重编 addon（dist/repack-ubuntu-amd64.sh）" >&2
    exit 1 ;;
esac

# Debian packages must not install into /usr/local (reserved for the administrator).
if [ -d "$STAGE/usr/local/share/fcitx5/addon" ]; then
  mkdir -p "$STAGE/usr/share/fcitx5/addon"
  mv "$STAGE/usr/local/share/fcitx5/addon/"* "$STAGE/usr/share/fcitx5/addon/"
  rm -rf "$STAGE/usr/local"
fi

# 1.5) config-ui (Qt5) → /usr
DESTDIR="$STAGE" cmake --install /priv/config-ui/build --prefix /usr >/dev/null 2>&1
if [ ! -x "$STAGE/usr/bin/fcitx5-rime-config" ]; then
  echo "!! config-ui 没有装进包里，打包中止" >&2
  exit 1
fi

# 2) librime + 插件 + 捆绑库 → 私有目录（build-22.04.sh 的产物在 /out）
mkdir -p "$STAGE$PRIVLIB/rime-plugins"
cp -a /out/librime/build/lib/librime.so* "$STAGE$PRIVLIB/"
cp -a /out/librime/build/lib/rime-plugins/*.so "$STAGE$PRIVLIB/rime-plugins/"
cp -a /out/lib/libctranslate2.so* "$STAGE$PRIVLIB/"
cp -a /out/bundle-libs/*.so* "$STAGE$PRIVLIB/" 2>/dev/null || true
# addon 与插件的 DT_NEEDED 都是 `librime.so.1`（SONAME），包里必须有这个名字。
# 曾经这里是 `rm -f librime.so.1`（想清掉 cp -a 带进来的相对链接），但它在
# bundle 拷贝之后执行，把真正的 so 也删了 —— 结果 `librime.so -> librime.so.1`
# 悬空，加载器只能去系统路径碰运气。改成显式建链，指向实际编出来的版本文件。
RIME_SO=$(cd "$STAGE$PRIVLIB" && ls librime.so.1.* 2>/dev/null | head -1)
[ -n "$RIME_SO" ] || { echo "!! 包里没找到 librime.so.1.*（librime 是构建产物，先跑 build-22.04.sh）" >&2; exit 1; }
ln -sf "$RIME_SO" "$STAGE$PRIVLIB/librime.so.1"
ln -sf librime.so.1 "$STAGE$PRIVLIB/librime.so"

# 3) 模型 + 默认配置 → /usr/share/fcitx5-rime/
D="$STAGE/usr/share/fcitx5-rime"
mkdir -p "$D/predict_models" "$D/seed"
cp -a /vendor/models/zh-base-ct2-int8 "$D/predict_models/"
# 种子配置是我们自己的（进私有库）；词库分片太大，按 vendor 约定另放。
cp -a /priv/data/ai-predict-defaults/*.custom.yaml "$D/seed/"
cp -a /vendor/dicts/. "$D/seed/"
cp -a /vendor/rime-ice/. "$D/seed/"

# 候选面板配色数据（21 套配色 + 全局外观，与 mac/Windows 同键名）。
# 少了它面板就没有品牌外观，所以缺文件直接失败，不静默出包。
if [ ! -f /priv/data/bangke.yaml ]; then
  echo "!! 找不到 /priv/data/bangke.yaml（配色真源），先同步源码再打包" >&2
  exit 1
fi
cp -a /priv/data/bangke.yaml "$D/seed/bangke.yaml"

python3 /priv/dist/lifecycle/opencc-seed.py "$STAGE"

# 4) ld.so.conf → 私有目录
mkdir -p "$STAGE/etc/ld.so.conf.d"
echo "$PRIVLIB" > "$STAGE/etc/ld.so.conf.d/fcitx5-rime-ai.conf"

# 品牌名不在这里改 —— 它直接写在物料里（src/rime-ai.conf.in、src/rime-ai-addon.conf.in.in、
# config-ui/data/fcitx5-rime-config.desktop、metainfo）。以前是打包时 sed 改，但 msgfmt 会拿
# po 展开出 Name[zh_CN]=中州韵 这类本地化键，漏改一个键就漏上游文案出去（Comment 就这么漏过）。
# 这里的 IM_NAME 只用来给「双击安装脚本」和 tar 起名。

# 5) 内置中文字体 → /usr/share/fonts/
mkdir -p "$STAGE/usr/share/fonts/truetype/fcitx5-rime"
cp -a /vendor/fonts/*.ttf /vendor/fonts/*.otf \
      "$STAGE/usr/share/fonts/truetype/fcitx5-rime/" 2>/dev/null || true

# 6) DEBIAN 元数据
mkdir -p "$STAGE/DEBIAN"
# 我们的 addon 已经改名成 rime-ai，addon 自己的文件不再与发行版同名；但设置程序的
# /usr/bin/fcitx5-rime-config 与 applications/*.desktop 仍与发行版 fcitx5-rime 同路径。
# 不声明关系的话，dpkg 会把两份当独立文件互相覆盖：先装的被顶掉，后卸载的一方走后，
# 另一方的文件也跟着没了。我们本来就是它的替代品，且共用同一个 rime 用户目录，
# 所以明确取代、不共存。（注意：control 文件里不能写注释，说明只能放这儿。）
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: Beijing Bangke Intelligent Technology Co., Ltd.
Depends: fcitx5, libc6, libstdc++6, libgcc-s1, libqt5widgets5, libqt5dbus5, python3, util-linux, sudo, qtwayland5, librime-data, im-config, fcitx5-frontend-gtk3, fcitx5-frontend-qt5, libglib2.0-bin
Conflicts: fcitx5-rime
Replaces: fcitx5-rime
Provides: fcitx5-rime
Description: Rime input method for Fcitx5 with AI prediction plugin
 Fcitx5 frontend for the Rime input method engine, bundled with the
 librime-ai-predict plugin (CTranslate2-based AI candidate prediction)
 and a Qt5 configuration UI.
EOF

bash /priv/dist/lifecycle/stage.sh "$STAGE" ubuntu
cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/bash
[ "${1:-}" = configure ] || exit 0
# Older postrm scripts mistakenly cleaned state even during upgrade. Restore our
# quarantined records only after dpkg has finished calling that old script.
if [ -d /var/lib/bangke-pinyin-upgrade-state ]; then
  mkdir -p /var/lib/bangke-pinyin
  cp -an /var/lib/bangke-pinyin-upgrade-state/. /var/lib/bangke-pinyin/
  rm -rf /var/lib/bangke-pinyin-upgrade-state
fi
bash /usr/lib/bangke-pinyin/package.sh install
EOF
cat > "$STAGE/DEBIAN/prerm" <<'EOF'
#!/bin/sh
exit 0
EOF
cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/bash
case "${1:-}" in
  remove|purge)
    [ ! -f /var/lib/bangke-pinyin/lifecycle/package.sh ] || bash /var/lib/bangke-pinyin/lifecycle/package.sh remove
    ;;
esac
exit 0
EOF
cat > "$STAGE/DEBIAN/preinst" <<'EOF'
#!/bin/bash
if [ "${1:-}" = upgrade ] && [ -d /var/lib/bangke-pinyin ]; then
  mkdir -p /var/lib/bangke-pinyin-upgrade-state
  cp -a /var/lib/bangke-pinyin/. /var/lib/bangke-pinyin-upgrade-state/
  rm -rf /var/lib/bangke-pinyin
fi
if grep -q '^Name=蚌壳拼音$' /usr/share/fcitx5/inputmethod/rime.conf 2>/dev/null; then
  mkdir -p /var/lib/bangke-pinyin
  touch /var/lib/bangke-pinyin/migrate-rime
  if [ -d /var/lib/bangke-pinyin-upgrade-state ]; then
    touch /var/lib/bangke-pinyin-upgrade-state/migrate-rime
  fi
fi
exit 0
EOF
chmod 0755 "$STAGE/DEBIAN/"{preinst,postinst,prerm,postrm}

# 7) 打包 → 私有侧 build-out/
mkdir -p "$OUT"
cd "$OUT"
rm -f "${PKG}_"*"_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$STAGE" "${PKG}_${VERSION}_${ARCH}.deb"
# 容器以 root 写入挂载目录，把属主交还给私有库的主人
chown -R "$(stat -c '%U:%G' /priv)" "$OUT" 2>/dev/null || true

# 生成「双击即可安装」的脚本（品牌名注入），与 deb 放在一起
install_src=/priv/dist/installer/install-template.sh
if [ -f "$install_src" ]; then
  sed "s/__IM_NAME__/$IM_NAME/g" "$install_src" > "$OUT/${IM_NAME}-安装.sh"
  chmod 0755 "$OUT/${IM_NAME}-安装.sh"
fi

# 打成 tar：**归档会保留可执行位** —— 从浏览器直接下载 .sh 的话它没有 +x，
# 双击只会用文本编辑器打开（这正是普通用户会踩的坑）。tar 解开就能双击运行。
# 版本号写进文件名：官网清单（bangke-site/scripts/gen-releases.mjs）只从文件名读版本。
tar -C "$OUT" -cf "$OUT/${IM_NAME}-${VERSION}-linux-x64.tar" \
    "${PKG}_${VERSION}_${ARCH}.deb" "${IM_NAME}-安装.sh"

echo "=== 产出 ==="
ls -lh "${PKG}_${VERSION}_${ARCH}.deb" "$OUT/${IM_NAME}-安装.sh" "$OUT/${IM_NAME}-${VERSION}-linux-x64.tar"
echo "=== 关键文件 ==="
dpkg-deb -c "${PKG}_${VERSION}_${ARCH}.deb" 2>/dev/null | grep -E "fcitx5/rime-ai\.so$|librime\.so\.1\.|ai-predict\.so|ld\.so\.conf|model\.bin$|fcitx5-rime-config|seed/bangke.yaml"
