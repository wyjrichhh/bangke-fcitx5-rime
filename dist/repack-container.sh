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

# 品牌名统一入口
[ -f /priv/dist/im-brand.env ] && . /priv/dist/im-brand.env
IM_NAME="${IM_NAME:-蚌壳拼音}"

VERSION="${VERSION:-5.0.12}"
ARCH=amd64
PKG=fcitx5-rime-ai-predict
PRIVLIB=/usr/lib/fcitx5-rime-ai
OUT=/priv/build-out
STAGE="$OUT/stage-final"

# 打包前必须确认私有部分真的重编过（历史上出过「包里是上一次的设置程序」）。
if [ ! -f /priv/build/src/rime.so ] && [ ! -f /priv/build/rime.so ]; then
  echo "!! 找不到 /priv/build 里的 rime.so，先跑 build-22.04.sh" >&2
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
# 把 rime.so 挪到 fcitx5 实际搜索的 multiarch 目录
mkdir -p "$STAGE/usr/lib/x86_64-linux-gnu/fcitx5"
if [ -f "$STAGE/usr/lib/fcitx5/rime.so" ]; then
  mv "$STAGE/usr/lib/fcitx5/rime.so" "$STAGE/usr/lib/x86_64-linux-gnu/fcitx5/rime.so"
  rmdir "$STAGE/usr/lib/fcitx5" 2>/dev/null || true
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

# 4) ld.so.conf → 私有目录
mkdir -p "$STAGE/etc/ld.so.conf.d"
echo "$PRIVLIB" > "$STAGE/etc/ld.so.conf.d/fcitx5-rime-ai.conf"

# 品牌名注入（改 im-brand.env 的 IM_NAME 后重跑即可全局改名）
for conf in "$STAGE/usr/share/fcitx5/inputmethod/rime.conf" \
            "$STAGE/usr/share/fcitx5/addon/rime.conf" \
            "$STAGE/usr/local/share/fcitx5/addon/rime.conf" \
            "$STAGE/usr/share/applications/fcitx5-rime-config.desktop"; do
  # 必须连本地化键一起换：fcitx5 按 locale 优先取 Name[zh_CN] 这种带后缀的键，
  # 只改裸 Name= 的话，中文 locale 用户看到的是上游残留的「中州韻」而不是品牌名。
  # （zh_CN/zh_TW 正好是我们的主要用户，实测在 Omarchy aarch64 上就是这样。）
  [ -f "$conf" ] && sed -i -E "s/^Name(\[[^]]*\])?=.*/Name\1=$IM_NAME/" "$conf"
done

# 5) 内置中文字体 → /usr/share/fonts/
mkdir -p "$STAGE/usr/share/fonts/truetype/fcitx5-rime"
cp -a /vendor/fonts/*.ttf /vendor/fonts/*.otf \
      "$STAGE/usr/share/fonts/truetype/fcitx5-rime/" 2>/dev/null || true

# 6) DEBIAN 元数据
mkdir -p "$STAGE/DEBIAN"
# 我们装的文件名与发行版 fcitx5-rime 完全同名（addon/rime.conf、
# inputmethod/rime.conf、applications/*.desktop）。不声明关系的话，dpkg 会把两份当
# 独立文件互相覆盖：先装的被顶掉，后卸载的一方走后，另一方的文件也跟着没了。我们本来
# 就是它的替代品，且共用同一个 rime 用户目录，所以明确取代、不共存。
# （注意：control 文件里不能写注释，说明只能放这儿。）
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: Beijing Bangke Intelligent Technology Co., Ltd.
Depends: fcitx5, libc6, libstdc++6, libgcc-s1, libqt5widgets5, libqt5dbus5
Conflicts: fcitx5-rime
Replaces: fcitx5-rime
Provides: fcitx5-rime
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
# 只补用户没有的文件（-n）：升级时不能把用户已有的 *.custom.yaml（AI 设置、
# 开关默认值、自定义方案）用出厂值覆盖掉 —— 与 mac 侧的 soft-write 一致。
seed() { h="$1"; [ -d "$h" ] || return 0; t="$h/$R"; mkdir -p "$t"; cp -an "$SEED"/. "$t"/; chown -R "$(stat -c '%U:%G' "$h")" "$t" 2>/dev/null || true; }
# 记下我们动过哪个家目录，卸载时照着回收。
STATE=/var/lib/bangke-pinyin
record() { mkdir -p "$STATE"; grep -qxF "$2" "$STATE/$1" 2>/dev/null || echo "$2" >> "$STATE/$1"; }
# classicui.conf 是 fcitx5 **框架级**的面板配置，对所有输入法生效 —— 用户已经有配置
# 就绝不覆盖（那等于替别的输入法改外观，卸载后还留着）。只在原本没有时给一份品牌
# 默认，并记下来，卸载时删掉、回到原样。
gen_theme() {
  h="$1"; [ -d "$h" ] || return 0
  if [ -e "$h/.config/fcitx5/conf/classicui.conf" ]; then
    echo "  $h 已有 classicui.conf，跳过主题生成（不覆盖用户面板配置）"
    return 0
  fi
  HOME="$h" fcitx5-rime-config --generate-theme >/dev/null 2>&1 || return 0
  record generated-classicui "$h"
  chown -R "$(stat -c '%U:%G' "$h")" \
      "$h/.local/share/fcitx5/themes" "$h/.config/fcitx5" 2>/dev/null || true
}
# 把 rime 注册进 fcitx5 的输入法列表。装完包 addon 只是"可用"，用户还得自己把它
# 加进输入法组才会出现 —— 普通用户不会做这一步。幂等，且不动用户已有顺序。
# 注意必须插在 [GroupOrder] 之前：profile 是分节 INI，追加到末尾会落进那节里，等于没注册。
register_im() {
  h="$1"; [ -d "$h" ] || return 0
  record registered-profile "$h"
  p="$h/.config/fcitx5/profile"
  if [ -f "$p" ] && grep -q '^Name=rime$' "$p"; then return 0; fi
  mkdir -p "$h/.config/fcitx5"
  if [ ! -f "$p" ]; then
    printf '[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=rime\n\n[Groups/0/Items/0]\nName=rime\nLayout=\n\n[GroupOrder]\n0=Default\n' > "$p"
  else
    n=$(grep -c '^\[Groups/0/Items/' "$p" 2>/dev/null || echo 0)
    item=$(printf '[Groups/0/Items/%s]\nName=rime\nLayout=\n' "$n")
    if grep -q '^\[GroupOrder\]' "$p"; then
      # 逐行重写、插在 [GroupOrder] 之前。
      # 不能用 awk -v：带换行的值会报 "newline in string"（实测踩过）。
      tmp="$p.new"; : > "$tmp"
      while IFS= read -r line || [ -n "$line" ]; do
        if [ "$line" = "[GroupOrder]" ]; then
          printf '%s\n\n' "$item" >> "$tmp"
        fi
        printf '%s\n' "$line" >> "$tmp"
      done < "$p"
      mv "$tmp" "$p"
    else
      printf '%s\n' "$item" >> "$p"
    fi
  fi
  chown "$(stat -c '%U:%G' "$h")" "$p" 2>/dev/null || true
}
seed /etc/skel
gen_theme /etc/skel
register_im /etc/skel
[ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ] && seed "$(getent passwd "$SUDO_USER" | cut -d: -f6)"
[ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ] && gen_theme "$(getent passwd "$SUDO_USER" | cut -d: -f6)"
[ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ] && register_im "$(getent passwd "$SUDO_USER" | cut -d: -f6)"
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
# 卸载要跟安装对称：安装时替用户写了什么，这里就得撤掉什么，否则会在用户的
# 输入法框架里留下指向已删引擎的死条目、以及我们替他做的框架级改动。
set -e
ldconfig || true

STATE=/var/lib/bangke-pinyin

# 从 profile 里摘掉我们加的那条输入法。只删「节体第一行是 Name=rime」的块 ——
# 我们写进去的正是这个形状，别人家的条目一概不动。
unregister_im() {
  h="$1"; [ -d "$h" ] || return 0
  p="$h/.config/fcitx5/profile"
  [ -f "$p" ] || return 0
  tmp="$p.bangke-uninstall"
  # 第一趟：摘掉我们写进去的那一段（形状就是「节体第一行是 Name=rime」，别人家的不动）。
  awk '
    /^\[Groups\/[0-9]+\/Items\/[0-9]+\]/ {
      hdr = $0; getline nxt
      if (nxt == "Name=rime") { getline; next }
      print hdr; print nxt; next
    }
    { print }
  ' "$p" > "$tmp" 2>/dev/null || { rm -f "$tmp"; return 0; }
  # 第二趟：同组内重新连续编号。留个洞（Items/2 前面没有 1）按组枚举的前端可能读不动。
  awk '
    /^\[Groups\/[0-9]+\/Items\/[0-9]+\]$/ {
      split($0, a, /[\/\[\]]+/); g = a[3]
      printf "[Groups/%s/Items/%d]\n", g, idx[g]++
      next
    }
    { print }
  ' "$tmp" > "$tmp.2" 2>/dev/null && mv "$tmp.2" "$p" || { rm -f "$tmp" "$tmp.2"; return 0; }
  rm -f "$tmp"
  # 首选项如果指着我们，清掉，别让前端去加载一个不存在的输入法。
  sed -i 's/^DefaultIM=rime$/DefaultIM=/' "$p" 2>/dev/null || true
  # profile 整份是我们建的（这个文件当初根本不存在），删干净后只剩个空组 —— 没必要
  # 留个空壳，直接删掉，回到「我们没来过」的样子。
  if ! grep -q '^\[Groups/[0-9]*/Items/' "$p" 2>/dev/null; then
    rm -f "$p"
  fi
}

# 面板配置（classicui.conf）只在「装之前本来没有」时才由我们生成 —— 那就删掉，
# 回到原样。用户自己已有的配置我们从没碰过，也就不动。
restore_classicui() {
  h="$1"; [ -d "$h" ] || return 0
  rm -f "$h/.config/fcitx5/conf/classicui.conf"
  rm -rf "$h/.local/share/fcitx5/themes/bangke"* 2>/dev/null || true
}

if [ -f "$STATE/registered-profile" ]; then
  while IFS= read -r h; do unregister_im "$h"; done < "$STATE/registered-profile"
fi
if [ -f "$STATE/generated-classicui" ]; then
  while IFS= read -r h; do restore_classicui "$h"; done < "$STATE/generated-classicui"
fi
rm -rf "$STATE"

exit 0
PEOF
chmod 0755 "$STAGE/DEBIAN/postrm"

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
dpkg-deb -c "${PKG}_${VERSION}_${ARCH}.deb" 2>/dev/null | grep -E "fcitx5/rime\.so$|librime\.so\.1\.|ai-predict\.so|ld\.so\.conf|model\.bin$|fcitx5-rime-config|seed/bangke.yaml"
