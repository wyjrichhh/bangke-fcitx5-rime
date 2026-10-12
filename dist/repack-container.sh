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

VERSION="${VERSION:-5.0.12}"
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
# 把 rime-ai.so 挪到 fcitx5 实际搜索的 multiarch 目录
mkdir -p "$STAGE/usr/lib/x86_64-linux-gnu/fcitx5"
if [ -f "$STAGE/usr/lib/fcitx5/rime-ai.so" ]; then
  mv "$STAGE/usr/lib/fcitx5/rime-ai.so" "$STAGE/usr/lib/x86_64-linux-gnu/fcitx5/rime-ai.so"
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
#!/bin/bash
# 装/升级后的初始化。与 dist/arch/bangke-pinyin.install 是**同一套语义的两份移植**
# （那份是权威实现，Arch 线上真机踩出来的）：出厂配置按键合并、把我们的输入法插到
# 输入法组第一位、写 profile 之前先停 fcitx5 —— 改一处请照着改另一处。
#
# 故意不开 set -e：这里全是「拿不到就跳过」的探测（fcitx5 没在跑、用户没有那个文件…），
# -e 会把正常情况当失败，还会让 postinst 报错退出、看起来像装坏了。
ldconfig || true
fc-cache -f >/dev/null 2>&1 || true
gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
update-desktop-database /usr/share/applications >/dev/null 2>&1 || true

SEED=/usr/share/fcitx5-rime/seed
STATE=/var/lib/bangke-pinyin
# 记下我们动过哪个家目录，卸载时照着回收。
_record() { mkdir -p "$STATE"; grep -qxF "$2" "$STATE/$1" 2>/dev/null || echo "$2" >> "$STATE/$1"; }
# 从旧版（addon 名 rime）升上来的迁移。
#
# 旧包在 profile 里注册的是 Name=rime、addon 自己的配置在 conf/rime.conf —— 两个名字都
# 跟着 addon 改成了 rime-ai。不改的话：profile 里那条会变成指向已删 addon 的死条目，
# 用户「在应用内显示拼音预编辑」的设置会被当成没设过、悄悄退回默认值。
# 就地改名（不挪位置），保留用户自己的输入法顺序。
_migrate_from_rime() {
  local h="$1"
  [ -n "$h" ] && [ -d "$h" ] || return 0
  local p="$h/.config/fcitx5/profile"
  if [ -f "$p" ]; then
    sed -i 's/^Name=rime$/Name=rime-ai/; s/^DefaultIM=rime$/DefaultIM=rime-ai/' "$p" 2>/dev/null || true
  fi
  local c="$h/.config/fcitx5/conf"
  if [ -f "$c/rime.conf" ] && [ ! -e "$c/rime-ai.conf" ]; then
    mv "$c/rime.conf" "$c/rime-ai.conf" 2>/dev/null || true
  fi
}
# 把 rime-ai 插到**第 0 组的第一位**。
#
# 为什么必须靠前：Ctrl+空格 是按组内顺序向后枚举的，排在末尾等于「按一下切到别的输入法」
# （真机 Omarchy 预装 chewing 时就是这么翻车的；deb 这边早先是追加到组尾）。
# 为什么只动第 0 组：只摘掉第 0 组的条目节、原地重排，别的输入法组、组内条目自己的
# Layout= 都原样保留。
_register_im_first() {
  local p="$1" tmp="$p.bangke-register"
  awk -v ours=rime-ai '
    BEGIN { n = 0; done_ = 0; in0 = 0 }   # n 必须初始化：awk 里未初始化是空串，
                                          # name[""] 不是 name[0]，第一个条目会被吞掉
    function emit(   i) {
      printf "[Groups/0/Items/0]\nName=%s\nLayout=\n\n", ours
      for (i = 0; i < n; i++) printf "[Groups/0/Items/%d]\nName=%s\n%s\n\n", i + 1, name[i], layout[i]
      done_ = 1
    }
    /^\[Groups\/0\/Items\/[0-9]+\]$/ { skip = 1; taken = 0; cur = -1; next }
    skip && /^\[/ { skip = 0 }
    skip {
      if (!taken && $0 ~ /^Name=/) {
        taken = 1
        nm = substr($0, 6)
        if (nm != "" && nm != ours && !(nm in dup)) { dup[nm] = 1; name[n] = nm; layout[n] = "Layout="; cur = n; n++ }
      } else if (cur >= 0 && $0 ~ /^Layout=/) {
        layout[cur] = $0
      }
      next
    }
    /^\[Groups\/0\]$/ { in0 = 1; print; next }
    in0 && !done_ && $0 ~ /^\[/ { emit() }
    in0 && !done_ && /^DefaultIM=/ { print "DefaultIM=" ours; next }
    { print }
    END { if (in0 && !done_) emit() }
  ' "$p" > "$tmp" 2>/dev/null || { rm -f "$tmp"; return 0; }
  mv "$tmp" "$p"
}

# 只把「产品拥有、设置页里没有入口」的键合并进用户已有的 custom 文件，其余原样保留。
#   default.custom.yaml 的 schema_list —— 默认方案
#   <方案>.custom.yaml 的 schema/name —— 方案显示名（「蚌壳-雾凇」这套前缀）。**每次升级
#     都按 seed 纠正**：切换输入法时那个气泡显示的是它推出来的短标签（「蚌-雾」，见
#     rimestate 的 shortSchemaLabel），`cp -an` 不覆盖老文件，不主动并进来老用户永远看不到
#   <方案>.custom.yaml 的 translator/spelling_hints —— **只在用户没设过时才补**
#     （设置页有「拼写提示」开关，那是用户的，不能每次升级都强行改回去）
_merge_factory_config() {
  local h="$1"
  local rime="$h/.local/share/fcitx5/rime"
  command -v python3 >/dev/null 2>&1 || return 0
  python3 - "$rime" "$SEED" <<'PYEOF' 2>/dev/null || true
import os, re, sys
rime, seed = sys.argv[1], sys.argv[2]

def lines(p):
    try:
        return open(p, encoding="utf-8").read().splitlines()
    except Exception:
        return None

# 1) 默认方案：把 seed 的 schema_list 整块搬到用户文件里（这是我们拥有、且设置页无入口的）
dst = os.path.join(rime, "default.custom.yaml")
src = os.path.join(seed, "default.custom.yaml")
d, s = lines(dst), lines(src)
if d is not None and s is not None:
    block = []
    grab = False
    for ln in s:
        if ln.strip().startswith("schema_list:"):
            grab = True
        elif grab and ln.strip() and not ln.startswith("    "):
            break
        if grab:
            block.append(ln)
    if block:
        out, i, replaced = [], 0, False
        while i < len(d):
            if d[i].strip().startswith("schema_list:"):
                out.extend(block); replaced = True
                i += 1
                while i < len(d) and (d[i].startswith("    ") or not d[i].strip()):
                    if not d[i].strip():
                        break
                    i += 1
                continue
            out.append(d[i]); i += 1
        if not replaced:
            for j, ln in enumerate(out):
                if ln.strip() == "patch:":
                    out[j+1:j+1] = block
                    break
        open(dst, "w", encoding="utf-8").write("\n".join(out) + "\n")

# 2) 方案文件：显示名（产品拥有，每次都纠正）与拼音注释（用户的，只补默认值）
for name in ("rime_ice.custom.yaml", "luna_pinyin.custom.yaml"):
    dst = os.path.join(rime, name)
    ls = lines(dst)
    if ls is None:
        continue
    seed_ls = lines(os.path.join(seed, name)) or []
    schema_name = next((l.strip() for l in seed_ls
                        if l.strip().startswith("schema/name:")), None)
    if schema_name and not any(l.strip().startswith("schema/name:") for l in ls):
        for j, ln in enumerate(ls):
            if ln.strip() == "patch:":
                ls.insert(j + 1, "  " + schema_name)
                break
    if schema_name:
        ls = ["  " + schema_name if l.strip().startswith("schema/name:") else l
              for l in ls]
    # 已经在迁移过的机器上就完全不动（标记见下面主流程）
    migrate = os.environ.get("BANGKE_SPELLING_MIGRATED") == "1"
    if any(l.strip().startswith("translator/spelling_hints:") for l in ls) and migrate:
        # 值是我们旧版默认写进去的（1），不是用户按开关改的 —— 一次性纠正为 0。
        # 之后那个键就归用户的「拼写提示」开关管，升级不再碰。
        ls = ["  translator/spelling_hints: 0"
              if l.strip().startswith("translator/spelling_hints:") else l for l in ls]
    elif not any(l.strip().startswith("translator/spelling_hints:") for l in ls):
        for j, ln in enumerate(ls):
            if ln.strip() == "patch:":
                ls[j+1:j+1] = ["  translator/spelling_hints: 0"]
                break
    open(dst, "w", encoding="utf-8").write("\n".join(ls) + "\n")
PYEOF
}

# 停/起 fcitx5 用的小工具。
#
# 为什么装包要先停它：**fcitx5 把输入法组放在内存里**，运行时被改动的 profile 会在
# 它退出或配置变更时按内存里的旧内容写回 —— 于是装包时注册进去的 rime 被抹掉，
# 表现就是「装完了却看不到蚌壳拼音」。真机上实测踩过两次（候选面板那条也是同一类）。
_BK_ENV=""
_BK_USER=""

# 停/起 fcitx5 用的小工具。
#
# 为什么装包要先停它：**fcitx5 把输入法组放在内存里**，运行时被改动的 profile 会在
# 它退出或配置变更时按内存里的旧内容写回 —— 于是装包时注册进去的 rime 被抹掉，
# 表现就是「装完了却看不到蚌壳拼音」。真机上实测踩过两次（候选面板那条也是同一类）。
#
# 能交给 systemd 就交给它：Omarchy 的 omarchy-fcitx5.service 是 `Restart=always`，
# 我们 pkill 之后它会自己拉一个新的（带 unit 里那套参数，托盘 drop-in 也才算数）；
# 我们再手动 `fcitx5 -r -d` 上去就是两个实例互抢，最后活下来的是谁看运气。
_BK_ENV=""
_BK_USER=""
_BK_UNIT="omarchy-fcitx5.service"

# 把抄下来的会话环境 + 用户名交给一个命令去跑（从 root 直接起会丢 WAYLAND_DISPLAY）
_bk_as_user() {
  [ -n "$_BK_USER" ] || return 1
  [ "$_BK_USER" = root ] && return 1
  command -v sudo >/dev/null 2>&1 || return 1
  if [ -n "$_BK_ENV" ]; then
    # shellcheck disable=SC2086
    sudo -u "$_BK_USER" -H env $_BK_ENV "$@" >/dev/null 2>&1
  else
    sudo -u "$_BK_USER" -H "$@" >/dev/null 2>&1
  fi
}

_stop_fcitx5() {
  _BK_ENV=""; _BK_USER=""
  local pid
  pid=$(pgrep -x fcitx5 | head -1) || true
  [ -n "$pid" ] || return 0
  # 必须走 /proc/<pid>：直接 stat "$pid" 是个相对路径、必然失败，_BK_USER 空掉之后
  # 末尾那次 _start_fcitx5 就静默不执行了（装完 fcitx5 一直没起来，只能重新登录）。
  _BK_USER=$(stat -c '%U' "/proc/$pid" 2>/dev/null)
  if [ -r "/proc/$pid/environ" ]; then
    local v line
    for v in WAYLAND_DISPLAY DISPLAY XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS \
             XDG_SESSION_TYPE XDG_CURRENT_DESKTOP; do
      line=$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -m1 "^$v=")
      [ -n "$line" ] && _BK_ENV="$_BK_ENV $line"
    done
  fi
  # 进程归 systemd 管就让 systemd 停（它不会 Restart=always 自己顶回来）
  if _bk_as_user systemctl --user is-active --quiet "$_BK_UNIT"; then
    _bk_as_user systemctl --user stop "$_BK_UNIT" || true
  fi
  pkill -x fcitx5 2>/dev/null || true
  local i
  for i in $(seq 1 10); do pgrep -x fcitx5 >/dev/null || break; sleep 1; done
  pkill -9 -x fcitx5 2>/dev/null || true
}

_start_fcitx5() {
  [ -n "$_BK_USER" ] || return 0
  [ "$_BK_USER" = root ] && return 0
  command -v sudo >/dev/null 2>&1 || return 0
  # 有 unit 就走 unit（它的 ExecStart 才会带上托盘 drop-in 的配置），但要确认真的起来了：
  # unit 里那条 ConditionEnvironment 不满足时 systemctl 也返回 0，却什么都没跑。
  if _bk_as_user systemctl --user restart "$_BK_UNIT"; then
    local i
    for i in $(seq 1 10); do
      pgrep -x fcitx5 >/dev/null && return 0
      sleep 1
    done
  fi
  if [ -n "$_BK_ENV" ]; then
    # shellcheck disable=SC2086
    sudo -u "$_BK_USER" -H env $_BK_ENV fcitx5 -r -d >/dev/null 2>&1 &
  else
    sudo -u "$_BK_USER" -H fcitx5 -r -d >/dev/null 2>&1 &
  fi
}

_start_fcitx5() {
  [ -n "$_BK_USER" ] || return 0
  [ "$_BK_USER" = root ] && return 0
  command -v sudo >/dev/null 2>&1 || return 0
  if [ -n "$_BK_ENV" ]; then
    # shellcheck disable=SC2086
    sudo -u "$_BK_USER" -H env $_BK_ENV fcitx5 -r -d >/dev/null 2>&1 &
  else
    sudo -u "$_BK_USER" -H fcitx5 -r -d >/dev/null 2>&1 &
  fi
}

_seed_user() {
  local h="$1"
  [ -n "$h" ] && [ -d "$h" ] || return 0
  local rime="$h/.local/share/fcitx5/rime"
  mkdir -p "$rime"
  # -n：不能覆盖用户已有的 *.custom.yaml（AI 设置、开关默认值、自定义方案）——
  # 与 mac 侧的 soft-write 一致。
  cp -an "$SEED"/. "$rime"/ 2>/dev/null || true
  _merge_factory_config "$h"

  # classicui.conf 是 fcitx5 **框架级**的面板配置，对所有输入法生效 —— 用户已经有配置
  # 就绝不覆盖（那等于替别的输入法改外观）。只在原本没有时给一份品牌默认，并记下来，
  # 卸载时删掉、回到原样。
  if [ -e "$h/.config/fcitx5/conf/classicui.conf" ]; then
    echo "  $h 已有 classicui.conf，跳过主题生成（不覆盖用户面板配置）"
  else
    HOME="$h" fcitx5-rime-config --generate-theme >/dev/null 2>&1 \
      && _record generated-classicui "$h"
  fi

  # 装完包 addon 只是「可用」，用户还得自己把它加进输入法组才会出现 —— 普通用户不会
  # 做这一步。
  #
  # 注册**无条件跑**（不是「缺了才补」）：上面生成主题那步会调 fcitx5-rime-config，而它
  # 自己也会把我们的输入法注册进 profile（插在末尾）。要是这里只在「缺了才补」，那条
  # 末尾的注册就会一直留着 —— Ctrl+空格 按一下先切到别的输入法，正是要避免的那个坑。
  # 注册时顺手做旧名迁移（见 _migrate_from_rime）。_register_im_first 幂等。
  _record registered-profile "$h"
  _migrate_from_rime "$h"
  local p="$h/.config/fcitx5/profile"
  mkdir -p "$(dirname "$p")"
  if [ ! -f "$p" ]; then
    printf '[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=rime-ai\n\n[Groups/0/Items/0]\nName=rime-ai\nLayout=\n\n[Groups/0/Items/1]\nName=keyboard-us\nLayout=\n\n[GroupOrder]\n0=Default\n' > "$p"
  else
    _register_im_first "$p"
  fi

  chown -R "$(stat -c '%U:%G' "$h" 2>/dev/null || echo root)" \
        "$rime" "$h/.config/fcitx5" "$h/.local/share/fcitx5/themes" 2>/dev/null || true
}

# 「拼写提示」的一次性迁移：老版本的默认值(=1)不是用户的选择，纠正一次；之后这个键归
# 用户的开关管，升级不再碰。标记放在 /var/lib 下。
BANGKE_SPELLING_MIGRATED=0
[ -f "$STATE/spelling-hints-migrated" ] || BANGKE_SPELLING_MIGRATED=1
export BANGKE_SPELLING_MIGRATED
mkdir -p "$STATE"

# 写用户配置之前先停 fcitx5（理由见 _stop_fcitx5）。
_stop_fcitx5
_seed_user /etc/skel
u="${SUDO_USER:-}"
if [ -n "$u" ] && [ "$u" != root ]; then
  _seed_user "$(getent passwd "$u" | cut -d: -f6)"
fi
touch "$STATE/spelling-hints-migrated"
# 用停之前抄下来的会话环境把它拉起来（从 root 里直接起会丢 WAYLAND_DISPLAY）。
_start_fcitx5
echo "蚌壳拼音：装好了。输入法已重新加载；若看不到「蚌壳拼音」，重新登录一次即可。"
exit 0
PEOF
chmod 0755 "$STAGE/DEBIAN/postinst"

cat > "$STAGE/DEBIAN/prerm" <<'PEOF'
#!/bin/sh
exit 0
PEOF
chmod 0755 "$STAGE/DEBIAN/prerm"

cat > "$STAGE/DEBIAN/postrm" <<'PEOF'
#!/bin/bash
# 卸载要跟安装对称：安装时替用户写了什么，这里就得撤掉什么，否则会在用户的
# 输入法框架里留下指向已删引擎的死条目、以及我们替他做的框架级改动。
# 与 dist/arch/bangke-pinyin.install 的 post_remove 同一套语义（连「先停 fcitx5、
# 收完再拉起来」这一步也一样：不停它，它退出时会按内存里的旧输入法组写回文件）。
# 同样不开 set -e：全是「拿不到就跳过」的探测。
ldconfig || true

STATE=/var/lib/bangke-pinyin
_BK_ENV=""
_BK_USER=""

# 停/起 fcitx5 用的小工具。
#
# 为什么装包要先停它：**fcitx5 把输入法组放在内存里**，运行时被改动的 profile 会在
# 它退出或配置变更时按内存里的旧内容写回 —— 于是装包时注册进去的 rime 被抹掉，
# 表现就是「装完了却看不到蚌壳拼音」。真机上实测踩过两次（候选面板那条也是同一类）。
_BK_ENV=""
_BK_USER=""

# 停/起 fcitx5 用的小工具。
#
# 为什么装包要先停它：**fcitx5 把输入法组放在内存里**，运行时被改动的 profile 会在
# 它退出或配置变更时按内存里的旧内容写回 —— 于是装包时注册进去的 rime 被抹掉，
# 表现就是「装完了却看不到蚌壳拼音」。真机上实测踩过两次（候选面板那条也是同一类）。
#
# 能交给 systemd 就交给它：Omarchy 的 omarchy-fcitx5.service 是 `Restart=always`，
# 我们 pkill 之后它会自己拉一个新的（带 unit 里那套参数，托盘 drop-in 也才算数）；
# 我们再手动 `fcitx5 -r -d` 上去就是两个实例互抢，最后活下来的是谁看运气。
_BK_ENV=""
_BK_USER=""
_BK_UNIT="omarchy-fcitx5.service"

# 把抄下来的会话环境 + 用户名交给一个命令去跑（从 root 直接起会丢 WAYLAND_DISPLAY）
_bk_as_user() {
  [ -n "$_BK_USER" ] || return 1
  [ "$_BK_USER" = root ] && return 1
  command -v sudo >/dev/null 2>&1 || return 1
  if [ -n "$_BK_ENV" ]; then
    # shellcheck disable=SC2086
    sudo -u "$_BK_USER" -H env $_BK_ENV "$@" >/dev/null 2>&1
  else
    sudo -u "$_BK_USER" -H "$@" >/dev/null 2>&1
  fi
}

_stop_fcitx5() {
  _BK_ENV=""; _BK_USER=""
  local pid
  pid=$(pgrep -x fcitx5 | head -1) || true
  [ -n "$pid" ] || return 0
  # 必须走 /proc/<pid>：直接 stat "$pid" 是个相对路径、必然失败，_BK_USER 空掉之后
  # 末尾那次 _start_fcitx5 就静默不执行了（装完 fcitx5 一直没起来，只能重新登录）。
  _BK_USER=$(stat -c '%U' "/proc/$pid" 2>/dev/null)
  if [ -r "/proc/$pid/environ" ]; then
    local v line
    for v in WAYLAND_DISPLAY DISPLAY XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS \
             XDG_SESSION_TYPE XDG_CURRENT_DESKTOP; do
      line=$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -m1 "^$v=")
      [ -n "$line" ] && _BK_ENV="$_BK_ENV $line"
    done
  fi
  # 进程归 systemd 管就让 systemd 停（它不会 Restart=always 自己顶回来）
  if _bk_as_user systemctl --user is-active --quiet "$_BK_UNIT"; then
    _bk_as_user systemctl --user stop "$_BK_UNIT" || true
  fi
  pkill -x fcitx5 2>/dev/null || true
  local i
  for i in $(seq 1 10); do pgrep -x fcitx5 >/dev/null || break; sleep 1; done
  pkill -9 -x fcitx5 2>/dev/null || true
}

_start_fcitx5() {
  [ -n "$_BK_USER" ] || return 0
  [ "$_BK_USER" = root ] && return 0
  command -v sudo >/dev/null 2>&1 || return 0
  # 有 unit 就走 unit（它的 ExecStart 才会带上托盘 drop-in 的配置），但要确认真的起来了：
  # unit 里那条 ConditionEnvironment 不满足时 systemctl 也返回 0，却什么都没跑。
  if _bk_as_user systemctl --user restart "$_BK_UNIT"; then
    local i
    for i in $(seq 1 10); do
      pgrep -x fcitx5 >/dev/null && return 0
      sleep 1
    done
  fi
  if [ -n "$_BK_ENV" ]; then
    # shellcheck disable=SC2086
    sudo -u "$_BK_USER" -H env $_BK_ENV fcitx5 -r -d >/dev/null 2>&1 &
  else
    sudo -u "$_BK_USER" -H fcitx5 -r -d >/dev/null 2>&1 &
  fi
}

_start_fcitx5() {
  [ -n "$_BK_USER" ] || return 0
  [ "$_BK_USER" = root ] && return 0
  command -v sudo >/dev/null 2>&1 || return 0
  if [ -n "$_BK_ENV" ]; then
    # shellcheck disable=SC2086
    sudo -u "$_BK_USER" -H env $_BK_ENV fcitx5 -r -d >/dev/null 2>&1 &
  else
    sudo -u "$_BK_USER" -H fcitx5 -r -d >/dev/null 2>&1 &
  fi
}

# 从 profile 里摘掉我们加的那条输入法。只删「节体第一行是 Name=rime-ai」的块 ——
# 我们写进去的正是这个形状，别人家的条目一概不动（发行版的 rime 是另一个 addon 名，
# 用户后来自己装回来的话不能被我们误删）。
unregister_im() {
  h="$1"; [ -d "$h" ] || return 0
  p="$h/.config/fcitx5/profile"
  [ -f "$p" ] || return 0
  tmp="$p.bangke-uninstall"
  # 第一趟：摘掉我们写进去的那一段（形状就是「节体第一行是 Name=rime-ai」，别人家的不动）。
  awk '
    /^\[Groups\/[0-9]+\/Items\/[0-9]+\]/ {
      hdr = $0; getline nxt
      if (nxt == "Name=rime-ai") { getline; next }
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
  sed -i 's/^DefaultIM=rime-ai$/DefaultIM=/' "$p" 2>/dev/null || true
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

# 先停 fcitx5 再动 profile，收完再把它拉起来（理由见 _stop_fcitx5）。
_stop_fcitx5
if [ -f "$STATE/registered-profile" ]; then
  while IFS= read -r h; do unregister_im "$h"; done < "$STATE/registered-profile"
fi
if [ -f "$STATE/generated-classicui" ]; then
  while IFS= read -r h; do restore_classicui "$h"; done < "$STATE/generated-classicui"
fi
rm -rf "$STATE"
ldconfig || true
# 让 systemd 用户实例重新读（这里对应卸载方向：drop-in 之类的东西没了不能还按旧的来）
u="${SUDO_USER:-$_BK_USER}"
if [ -n "$u" ] && [ "$u" != root ]; then
  rt="/run/user/$(id -u "$u" 2>/dev/null)"
  [ -d "$rt" ] && sudo -u "$u" XDG_RUNTIME_DIR="$rt" \
    systemctl --user daemon-reload >/dev/null 2>&1 || true
fi
_start_fcitx5

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
dpkg-deb -c "${PKG}_${VERSION}_${ARCH}.deb" 2>/dev/null | grep -E "fcitx5/rime-ai\.so$|librime\.so\.1\.|ai-predict\.so|ld\.so\.conf|model\.bin$|fcitx5-rime-config|seed/bangke.yaml"
