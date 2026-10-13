#!/usr/bin/env bash
# 蚌壳拼音 —— Arch 系（aarch64 / x86_64）出包。同一份脚本，两个架构各在自己的容器里跑：
#
#   aarch64（Mac 本机，Apple Silicon 原生）：
#     docker run --rm \
#       -v ~/dev/bangke/bangke-fcitx5-rime:/priv \
#       -v ~/dev/bangke/bangke-librime:/librime:ro \
#       -v ~/dev/bangke/bangke-ai-predict:/plugin:ro \
#       -v ~/bangke/vendor:/vendor:ro \
#       -v ~/bangke/build-out/aarch64:/out \
#       menci/archlinuxarm:base bash /priv/dist/build-arch.sh
#
#   x86_64（在构建机 17 上跑，那边原生 x86_64；Mac 上编 amd64 慢且老被本机代理干扰）：
#     docker run --rm \
#       -v ~/bangke/commit/fcitx5-rime:/priv \
#       -v ~/bangke/commit/bangke-librime:/librime:ro \
#       -v ~/bangke/commit/bangke-ai-predict:/plugin:ro \
#       -v ~/bangke/vendor:/vendor:ro \
#       -v ~/bangke/build-out/x86_64:/out \
#       archlinux:latest bash /priv/dist/build-arch.sh
#
# 产物：/out/bangke-pinyin-<版本>-<pkgrel>-<架构>.pkg.tar.zst（+ 未打包的 bangke-pkg 安装树）。
# 架构不写死在脚本里：PKGBUILD 声明了 arch=('aarch64' 'x86_64')，makepkg 自己认。
#
# 与 Ubuntu 线同一套约定（/priv + /librime + /plugin + /vendor + /out），平台差异只有容器
# 镜像与包格式；产物互不通用 —— Arch 是滚动发行版，glibc 方向与 22.04 相反。
#
# 两个复用开关：
#   SKIP_DEPS=1    依赖已装好的环境跳过联网装包（把出过包的容器 commit 成镜像再跑即可；
#                  本机 fake-ip 代理会掐掉 archlinuxarm 镜像站，那时全靠它）
#   SKIP_LIBRIME=1 复用 /out/src 里已有的 CT2 与 librime 构建树（只改了 addon/打包时用）

set -euo pipefail

PRIV="${PRIV:-/priv}"
OUT="${OUT:-/out}"
SRC=/out/src          # 容器内的构建工作区（/out 是持久化的，便于 SKIP_LIBRIME 复用）
VERSION="${VERSION:-5.0.14}"
SKIP_LIBRIME="${SKIP_LIBRIME:-0}"
SKIP_DEPS="${SKIP_DEPS:-0}"

# ---------------------------------------------------------------- 依赖
echo "=== [1/7] 安装依赖 ==="
# SKIP_DEPS=1：依赖已经装好的环境（把出过包的容器 commit 成镜像）跳过联网装包。
# 镜像站被本机 fake-ip 代理劫持或抽风时，pacman 会以「Operation too slow」收场，
# 而这一步失败会直接把整个脚本带走 —— 那时用这个开关，不必等网络。
if [ "$SKIP_DEPS" != "1" ]; then
  # 容器里的 pacman 下载沙箱要 Landlock，某些内核上不可用；失败就关掉重来。
  pacman -Syu --noconfirm || { sed -i '/^\[options\]/a DisableSandbox' /etc/pacman.conf; pacman -Syu --noconfirm; }
  for _ in 1 2 3; do
    pacman -S --needed --noconfirm \
      base-devel cmake ninja git extra-cmake-modules gettext pkgconf sudo \
      fcitx5 qt5-base boost google-glog yaml-cpp gflags leveldb marisa opencc capnproto lua \
      && break
  done
fi

# Arch 滚动版把 cmake 4.x 上对 cmake_minimum_required(<3.5) 的兼容移除了，而几个第三方
# 工程的 CMakeLists 还写着老版本号 —— 统一放行，不改上游源码。
CMAKE_OLD_POLICY=(-DCMAKE_POLICY_VERSION_MINIMUM=3.5)

mkdir -p "$SRC" "$OUT"

# ---------------------------------------------------------------- CTranslate2
# **必须是共享库**，且 RUY 打开：插件链的是 libctranslate2.so.4（ruy 编在里面）。
# 照插件自带的 deps.mk 编成静态，插件会留一堆未定义的 ruy 符号 —— 表现是 addon 能加载、
# AI 预测静默失效。
if [ "$SKIP_LIBRIME" = "1" ] && [ -d "$SRC/ct2/build" ]; then
  echo "=== [2-4/7] 复用 $SRC 里的 CT2 与 librime 构建树 ==="
  cmake --build "$SRC/ct2/build" --target install
  cmake --install "$SRC/librime/build" --prefix /usr/local
else
  echo "=== [2/7] 构建 CTranslate2 (共享, Ruy 后端) ==="
  [ -f /vendor/ctranslate2/third_party/ruy/third_party/cpuinfo/CMakeLists.txt ] \
    || { echo "缺 /vendor/ctranslate2/.../cpuinfo（见 dist/stage-vendor.sh）" >&2; exit 1; }
  rm -rf "$SRC/ct2"
  cp -a /vendor/ctranslate2 "$SRC/ct2"
  cd "$SRC/ct2"
  cmake . -Bbuild -G Ninja -DBUILD_SHARED_LIBS=ON -DBUILD_CLI=OFF -DBUILD_TESTS=OFF \
    -DWITH_CUDA=OFF -DWITH_MKL=OFF -DWITH_OPENBLAS=OFF -DWITH_RUY=ON -DWITH_ACCELERATE=OFF \
    -DOPENMP_RUNTIME=NONE -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_INSTALL_RPATH=/usr/lib/fcitx5-rime-ai -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    "${CMAKE_OLD_POLICY[@]}"
  cmake --build build -j "$(nproc)"
  cmake --build build --target install

  echo "=== [3/7] 构建 librime + 插件 ==="
  rm -rf "$SRC/librime"
  cp -a /librime "$SRC/librime"
  # 私有库里带着 macOS 侧 `make deps` 的产物：lib/*.a 是 Mach-O 归档，Linux 链接器
  # 会把它们当依赖去找符号、然后满屏 undefined reference；include/ 下的第三方旧头
  # 会和系统库 API 对不上。两者都得清掉（Linux 一律用系统库）。
  rm -rf "$SRC/librime/lib" "$SRC/librime/build"
  rm -rf "$SRC/librime/include/glog" "$SRC/librime/include/yaml-cpp" \
         "$SRC/librime/include/leveldb" "$SRC/librime/include/marisa" \
         "$SRC/librime/include/opencc" "$SRC/librime/include/gtest"
  rm -rf "$SRC/librime/plugins/librime-ai-predict"
  mkdir -p "$SRC/librime/plugins/librime-ai-predict"
  cp -a /plugin/. "$SRC/librime/plugins/librime-ai-predict/"
  rm -rf "$SRC/librime/plugins/librime-ai-predict/deps" \
         "$SRC/librime/plugins/librime-ai-predict/build"
  # clone 不到的上游插件随 vendor 走（构建机/容器连不上 GitHub）。
  for p in librime-lua; do
    rm -rf "$SRC/librime/plugins/${p#librime-}"
    cp -a "/vendor/$p" "$SRC/librime/plugins/${p#librime-}"
  done
  # 物料是从 Mac 传过去的，可能夹带 AppleDouble（._*）—— 那些二进制头会被当成
  # 源码编（"stray '\5' in program"）。清一遍，别指望上游保持干净。
  find "$SRC/librime/plugins" -name '._*' -delete 2>/dev/null || true
  find "$SRC/librime/plugins" -name '.DS_Store' -delete 2>/dev/null || true
  cd "$SRC/librime"
  rm -rf build
  # 注：这里**不**传 Boost_NO_BOOST_CMAKE —— 那是 Ubuntu 22.04（boost 1.74）的处方，
  # Arch 的 boost 是新版、走 BoostConfig，强制 Module 模式反而可能找不到。
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_SHARED_LIBS=ON -DBUILD_MERGED_PLUGINS=OFF -DENABLE_EXTERNAL_PLUGINS=ON \
    -DBUILD_TEST=OFF -DBUILD_SAMPLE=OFF -DCTRANSLATE2_ROOT=/usr/local \
    "${CMAKE_OLD_POLICY[@]}"
  ninja -C build -j "$(nproc)"
  cmake --install build
fi

# 插件里不该残留 ruy 符号 —— 留着就是上面那个静默失效。
SO="$SRC/librime/build/lib/rime-plugins/librime-ai-predict.so"
RUY_UNDEF=$(nm -D --undefined-only "$SO" | grep -c ruy || true)
[ "$RUY_UNDEF" -eq 0 ] || { echo "插件残留 $RUY_UNDEF 个未定义 ruy 符号，AI 预测会失效" >&2; exit 1; }

# ---------------------------------------------------------------- addon / 设置程序
echo "=== [4/7] 构建 fcitx5-rime addon ==="
cd "$PRIV"
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/usr/local -DRIME_DATA_DIR=/usr/share/rime-data "${CMAKE_OLD_POLICY[@]}"
ninja -C build -j "$(nproc)"

echo "=== [5/7] 构建 config-ui (Qt5) ==="
rm -rf "$PRIV/config-ui/build"
cmake -S "$PRIV/config-ui" -B "$PRIV/config-ui/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr "${CMAKE_OLD_POLICY[@]}"
ninja -C "$PRIV/config-ui/build" -j "$(nproc)"

# ---------------------------------------------------------------- 组装安装树
echo "=== [6/7] 组装安装树 → $OUT/bangke-pkg ==="
PKG="$OUT/bangke-pkg"
rm -rf "$PKG"
mkdir -p "$PKG"/{usr/lib/fcitx5,usr/lib/fcitx5-rime-ai/rime-plugins,usr/bin,etc/ld.so.conf.d,usr/share/fcitx5/{addon,inputmethod},usr/share/applications}

cp "$PRIV/build/src/rime-ai.so"                     "$PKG/usr/lib/fcitx5/rime-ai.so"
cp /usr/local/lib/libctranslate2.so*                "$PKG/usr/lib/fcitx5-rime-ai/"
cp "$SRC/librime/build/lib/librime.so.1."*          "$PKG/usr/lib/fcitx5-rime-ai/"
# addon 与插件的 DT_NEEDED 都是 SONAME librime.so.1，包里必须有这个名字。
RIME_SO=$(cd "$PKG/usr/lib/fcitx5-rime-ai" && ls librime.so.1.* | head -1)
ln -sf "$RIME_SO" "$PKG/usr/lib/fcitx5-rime-ai/librime.so.1"
ln -sf librime.so.1 "$PKG/usr/lib/fcitx5-rime-ai/librime.so"
# 四个插件一起装：rime_ice 依赖 lua，缺了会刷上百条 error creating translator。
cp "$SRC/librime/build/lib/rime-plugins/"*.so       "$PKG/usr/lib/fcitx5-rime-ai/rime-plugins/"

# addon 与设置程序的**数据文件**（/usr/share/fcitx5/addon/rime-ai.conf、inputmethod/rime-ai.conf、
# desktop、图标…）走各自的 cmake install 规则 —— 缺了 addon conf，fcitx5 根本不知道有
# 这个输入法：装包不报错，但重启后切不出来。（Ubuntu 线一直是用 install 拿的这些。）
DESTDIR="$PKG" cmake --install "$PRIV/build" --prefix /usr
DESTDIR="$PKG" cmake --install "$PRIV/config-ui/build" --prefix /usr

# addon 描述文件由 src/rime-ai-addon.conf.in.in 生成，装到 ${FCITX_INSTALL_PKGDATADIR}/addon
# —— 那个变量在容器里会解析成 /usr/local/share/fcitx5，而 fcitx5 在 Arch 上只扫
# /usr/share/fcitx5。**这是最初「装完切不出输入法」的根因**：文件在，但位置不对，
# fcitx5 压根不知道有这个 addon。挪到它认的位置，别留 /usr/local 那棵树。
if [ -f "$PKG/usr/local/share/fcitx5/addon/rime-ai.conf" ]; then
  mkdir -p "$PKG/usr/share/fcitx5/addon"
  mv "$PKG/usr/local/share/fcitx5/addon/rime-ai.conf" "$PKG/usr/share/fcitx5/addon/rime-ai.conf"
fi
rm -rf "$PKG/usr/local"

# config-ui 的 install 规则不装二进制，显式放进去。
cp "$PRIV/config-ui/build/fcitx5-rime-config" "$PKG/usr/bin/"

# Arch 的库目录是 /usr/lib；addon 的 conf 也必须落在 /usr/share/fcitx5 ——
# deb 用的 /usr/local/share 在 Arch 上扫不到（实测 fcitx5-diagnose 数不到 Rime）。
echo /usr/lib/fcitx5-rime-ai > "$PKG/etc/ld.so.conf.d/fcitx5-rime-ai.conf"

# 托盘图标：Omarchy 的 omarchy-fcitx5.service 默认写着 `--disable notificationitem`
# （它的理由是「会和栏自己画的指示器重复」）。这份 drop-in 把它打开，托盘里才有 fcitx5 的
# 状态图标 —— 右键菜单里挂着「蚌壳拼音 设置 / 部署 / 同步」。别的 Arch 机器上没有那个
# unit，文件躺着不生效；装了的人想恢复原状就删掉它（文件头注释里写了命令）。
mkdir -p "$PKG/etc/systemd/user/omarchy-fcitx5.service.d"
cp "$PRIV/dist/arch/10-bangke-tray.conf" "$PKG/etc/systemd/user/omarchy-fcitx5.service.d/"

# 品牌名不在这里做 —— 它直接写在物料里（src/rime-ai.conf.in、src/rime-ai-addon.conf.in.in、
# config-ui/data/fcitx5-rime-config.desktop、metainfo）。以前是打包时 sed 改，但 msgfmt 会拿
# po 展开出 Name[zh_CN]=中州韵 这类本地化键，漏改一个键就漏上游文案出去（Comment 就这么漏过）。

# 数据部分与 Ubuntu 线同源：种子配置来自私有库，词库/字体/模型来自 vendor。
mkdir -p "$PKG/usr/share/fcitx5-rime/seed"
cp -a "$PRIV/data/ai-predict-defaults/"*.custom.yaml "$PKG/usr/share/fcitx5-rime/seed/"
cp -a /vendor/dicts/.                                "$PKG/usr/share/fcitx5-rime/seed/"
cp -a /vendor/rime-ice/.                             "$PKG/usr/share/fcitx5-rime/seed/"
mkdir -p "$PKG/usr/share/fcitx5-rime/predict_models"
# 只随包默认那个（出厂种子的 model_path 指向它）。多塞一个模型会让包大近一倍，
# 想试别的模型由设置页的「选择目录…」指过去。
DEFAULT_MODEL="${DEFAULT_MODEL:-zh-base-ct2-int8}"
cp -a "/vendor/models/$DEFAULT_MODEL"                "$PKG/usr/share/fcitx5-rime/predict_models/"
cp -a "$PRIV/data/bangke.yaml"                       "$PKG/usr/share/fcitx5-rime/seed/bangke.yaml"
mkdir -p "$PKG/usr/share/fonts/truetype/fcitx5-rime"
cp -a /vendor/fonts/*.ttf /vendor/fonts/*.otf "$PKG/usr/share/fonts/truetype/fcitx5-rime/" 2>/dev/null || true

# makepkg 跑在 fakeroot 里，会把文件的 uid/gid 原样记进包 —— 而 /out 是从 Mac 挂进来的，
# 那边的文件在里面显示成 uid 501（Mac 的 rich）。cp -a 会把它带进安装树，装到用户机器上
# 就出现「包内某个目录属主是 501」。统一压回 root:root，别把宿主机的 uid 漏进包。
chown -R 0:0 "$PKG" 2>/dev/null || true

# ---------------------------------------------------------------- 打 pacman 包
echo "=== [7/7] makepkg ==="
# makepkg 拒绝以 root 跑，建个普通用户来打包。
id -u builder >/dev/null 2>&1 || useradd -m builder
BUILD="$OUT/pkgbuild"
rm -rf "$BUILD"; mkdir -p "$BUILD"
cp "$PRIV/dist/arch/PKGBUILD" "$PRIV/dist/arch/bangke-pinyin.install" "$BUILD/"
cp -a "$PKG" "$BUILD/bangke-pkg"
chown -R builder "$BUILD"
cd "$BUILD"
# 版本号从 PKGBUILD 读，避免两处写死。
sed -i "s/^pkgver=.*/pkgver=$VERSION/" PKGBUILD
# --nodeps：depends 里是**运行期**依赖（qt5-wayland、librime-data 等），打包容器里
# 不一定装了；它们照常写进包元数据，由目标机器上的 pacman 负责满足。
sudo -u builder env PKGEXT='.pkg.tar.zst' makepkg -f --noconfirm --nodeps
# 包挪到 /out 顶层（makepkg 落在它的构建目录里），其余中间产物留在 $BUILD。
mv "$BUILD"/*.pkg.tar.* "$OUT/" 2>/dev/null || true
# 产物交还给挂载目录的主人（容器以 root 写 /out）。中间产物里有 .git 的 pack 文件
# 属于别的用户，chown 会报错 —— 与出包无关，别让它掀掉整个脚本。
chown -R "$(stat -c '%U:%G' "$OUT")" "$OUT" 2>/dev/null || true
ls -lh "$OUT"/*.pkg.tar.*
echo "=== 完成 ==="
