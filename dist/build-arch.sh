#!/usr/bin/env bash
# Build isolated Bangke packages in a native Arch container for each architecture.
# Mount frontend=/priv, engine=/librime, plugin=/plugin, assets=/vendor, output=/out.
# See LINUX_BUILD.md for container commands and vendor requirements.
# SKIP_DEPS=1 reuses system dependencies; SKIP_LIBRIME=1 reuses matching builds.

set -euo pipefail
JOBS="${JOBS:-4}"

PRIV="${PRIV:-/priv}"
OUT="${OUT:-/out}"
SRC="${SRC:-$OUT/src}"
PREFIX="${BUILD_PREFIX:-$OUT/prefix}"
LIBRIME_SRC="${LIBRIME_SRC:-/librime}"
PLUGIN_SRC="${PLUGIN_SRC:-/plugin}"
VENDOR="${VENDOR:-/vendor}"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
VERSION="${VERSION:-5.0.21}"
SKIP_LIBRIME="${SKIP_LIBRIME:-0}"
SKIP_DEPS="${SKIP_DEPS:-0}"
MAIN_BASELINE="${BANGKE_MAIN_BASELINE:-origin/main}"
MAIN_COMMIT=$(git -C "$PRIV" rev-parse --verify "$MAIN_BASELINE^{commit}")
BUILD_COMMIT=$(git -C "$PRIV" rev-parse --verify HEAD)
git -C "$PRIV" merge-base --is-ancestor "$MAIN_COMMIT" "$BUILD_COMMIT" \
  || { echo "Refusing to package a branch that does not contain main: $MAIN_COMMIT" >&2; exit 1; }
printf 'Main baseline: %s\nBuild commit: %s\nPlatform: %s\n' "$MAIN_COMMIT" "$BUILD_COMMIT" "$(uname -m)"


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
      base-devel cmake ninja git extra-cmake-modules gettext pkgconf sudo python \
      fcitx5 librime-data qt5-base boost google-glog yaml-cpp gflags leveldb marisa opencc capnproto lua \
      && break
  done
fi

# Arch 滚动版把 cmake 4.x 上对 cmake_minimum_required(<3.5) 的兼容移除了，而几个第三方
# 工程的 CMakeLists 还写着老版本号 —— 统一放行，不改上游源码。
CMAKE_OLD_POLICY=(-DCMAKE_POLICY_VERSION_MINIMUM=3.5)

mkdir -p "$SRC" "$OUT"

# ---------------------------------------------------------------- CTranslate2
# CT2 and its static backend archives are linked into the private runtime.
# No Rime/CT2 C++ symbols escape that runtime; only bangke_rime_get_api is public.
if [ "$SKIP_LIBRIME" = "1" ]; then
  [ -f "$SRC/librime/build/lib/libbangke-rime.so.1" ] \
    || { echo "SKIP_LIBRIME requires an isolated runtime build; rebuild without it" >&2; exit 1; }
  echo "=== [2-4/7] 复用 $SRC 里的 CT2 与 librime 构建树 ==="
  cmake --build "$SRC/ct2/build" --target install
  cmake --install "$SRC/librime/build" --prefix "$PREFIX"
else
  echo "=== [2/7] 构建 CTranslate2 (静态, Ruy 后端) ==="
  [ -f ${VENDOR}/ctranslate2/third_party/ruy/third_party/cpuinfo/CMakeLists.txt ] \
    || { echo "缺 ${VENDOR}/ctranslate2/.../cpuinfo（见 dist/stage-vendor.sh）" >&2; exit 1; }
  rm -rf "$SRC/ct2"
  mkdir -p "$SRC/ct2"
  cp -a "$VENDOR/ctranslate2/." "$SRC/ct2/"
  cd "$SRC/ct2"
  cmake . -Bbuild -G Ninja -DBUILD_SHARED_LIBS=OFF -DBUILD_CLI=OFF -DBUILD_TESTS=OFF \
    -DWITH_CUDA=OFF -DWITH_MKL=OFF -DWITH_OPENBLAS=OFF -DWITH_RUY=ON -DWITH_ACCELERATE=OFF \
    -DOPENMP_RUNTIME=NONE -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_INSTALL_RPATH=/usr/lib/fcitx5-rime-ai -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    "${CMAKE_OLD_POLICY[@]}"
  cmake --build build -j "$JOBS"
  cmake --build build --target install

  echo "=== [3/7] 构建 librime + 插件 ==="
  rm -rf "$SRC/librime"
  cp -a "$LIBRIME_SRC" "$SRC/librime"
  # 私有库里带着 macOS 侧 `make deps` 的产物：lib/*.a 是 Mach-O 归档，Linux 链接器
  # 会把它们当依赖去找符号、然后满屏 undefined reference；include/ 下的第三方旧头
  # 会和系统库 API 对不上。两者都得清掉（Linux 一律用系统库）。
  rm -rf "$SRC/librime/lib" "$SRC/librime/build"
  rm -rf "$SRC/librime/include/glog" "$SRC/librime/include/yaml-cpp" \
         "$SRC/librime/include/leveldb" "$SRC/librime/include/marisa" \
         "$SRC/librime/include/opencc" "$SRC/librime/include/gtest"
  rm -rf "$SRC/librime/plugins/ai-predict" "$SRC/librime/plugins/librime-ai-predict"
  mkdir -p "$SRC/librime/plugins/librime-ai-predict"
  cp -a "$PLUGIN_SRC"/. "$SRC/librime/plugins/librime-ai-predict/"
  rm -rf "$SRC/librime/plugins/librime-ai-predict/deps" \
         "$SRC/librime/plugins/librime-ai-predict/build"
  # clone 不到的上游插件随 vendor 走（构建机/容器连不上 GitHub）。
  for p in librime-lua; do
    rm -rf "$SRC/librime/plugins/${p#librime-}"
    cp -a "${VENDOR}/$p" "$SRC/librime/plugins/${p#librime-}"
  done
  # 物料是从 Mac 传过去的，可能夹带 AppleDouble（._*）—— 那些二进制头会被当成
  # 源码编（"stray '\5' in program"）。清一遍，别指望上游保持干净。
  find "$SRC/librime/plugins" -name '._*' -delete 2>/dev/null || true
  find "$SRC/librime/plugins" -name '.DS_Store' -delete 2>/dev/null || true
  cd "$SRC/librime"
  rm -rf build
  # 注：这里**不**传 Boost_NO_BOOST_CMAKE —— 那是 Ubuntu 22.04（boost 1.74）的处方，
  # Arch 的 boost 是新版、走 BoostConfig，强制 Module 模式反而可能找不到。
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_SHARED_LIBS=ON -DBUILD_SEPARATE_LIBS=OFF -DBUILD_MERGED_PLUGINS=ON -DENABLE_EXTERNAL_PLUGINS=OFF \
    -DCMAKE_POLICY_DEFAULT_CMP0167=NEW \
    -DBUILD_TEST=OFF -DBUILD_TOOLS=OFF -DBUILD_SAMPLE=OFF -DCTRANSLATE2_ROOT="$PREFIX" \
    -DCTRANSLATE2_LIBRARY="$PREFIX/lib/libctranslate2.a" \
    -DBANGKE_CT2_BUILD_DIR="$SRC/ct2/build" \
    -DCMAKE_PROJECT_rime_INCLUDE="$PRIV/dist/arch/isolate-runtime.cmake" \
    "${CMAKE_OLD_POLICY[@]}"
  ninja -C build -j "$JOBS"
  cmake --install build
fi

# Reject exports that could bind another addon to Bangke's singleton state.
SO="$SRC/librime/build/lib/libbangke-rime.so.1"
EXPORTS=$(nm -D --defined-only --format=posix "$SO" | awk '{print $1}')
[ "$EXPORTS" = bangke_rime_get_api ] \
  || { echo "Unexpected private runtime exports: $EXPORTS" >&2; exit 1; }

# ---------------------------------------------------------------- addon / 设置程序
echo "=== [4/7] 构建 fcitx5-rime addon ==="
cd "$PRIV"
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_PREFIX_PATH="$PREFIX" -DBANGKE_ISOLATED_RUNTIME=ON -DBANGKE_PRIVATE_PANEL=ON \
  -DRIME_DATA_DIR=/usr/share/bangke-pinyin/rime-data "${CMAKE_OLD_POLICY[@]}"
ninja -C build -j "$JOBS"
ADDON_DYNAMIC=$(readelf -d "$PRIV/build/src/rime-ai.so")
case "$ADDON_DYNAMIC" in
  *"[libbangke-rime.so.1]"*) ;;
  *) echo "Addon is not linked to the private runtime" >&2; exit 1 ;;
esac
case "$ADDON_DYNAMIC" in
  *"[librime.so."*) echo "Addon still depends on system Rime" >&2; exit 1 ;;
esac

echo "=== [5/7] 构建 config-ui (Qt5) ==="
rm -rf "$PRIV/config-ui/build"
cmake -S "$PRIV/config-ui" -B "$PRIV/config-ui/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr "${CMAKE_OLD_POLICY[@]}"
ninja -C "$PRIV/config-ui/build" -j "$JOBS"

# ---------------------------------------------------------------- 组装安装树
echo "=== [6/7] 组装安装树 → $OUT/bangke-pkg ==="
PKG="$OUT/bangke-pkg"
rm -rf "$PKG"
mkdir -p "$PKG"/{usr/lib/fcitx5,usr/lib/fcitx5-rime-ai,usr/bin,etc,usr/share/fcitx5/{addon,inputmethod},usr/share/applications}

cp "$PRIV/build/src/rime-ai.so"                     "$PKG/usr/lib/fcitx5/rime-ai.so"
cp -a "$SRC/librime/build/lib/"libbangke-rime.so* "$PKG/usr/lib/fcitx5-rime-ai/"
# Shared Rime data is read-only input. Keep the deployed copy and OpenCC
# compatibility files private so installation never modifies system Rime data.
mkdir -p "$PKG/usr/share/bangke-pinyin/rime-data"
for data in /usr/share/rime-data/*; do
  case "${data##*/}" in
    predict_models|ai_pinyin.schema.yaml) continue ;;
  esac
  cp -aL "$data" "$PKG/usr/share/bangke-pinyin/rime-data/"
done


# Normalize OpenCC compatibility data before packaging, so package checksums
# describe the installed files and hooks never rewrite package-owned content.
python3 - "$PKG/usr/share/bangke-pinyin/rime-data/opencc" <<'PYOPENCC'
import json
from pathlib import Path
import sys
source = Path('/usr/share/opencc')
destination = Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)
for config in source.glob('*.json'):
    data = json.loads(config.read_text())
    data.pop('normalization', None)
    (destination / config.name).write_text(json.dumps(data) + '\n')
for dictionary in source.glob('*.ocd2'):
    target = destination / dictionary.name
    if target.exists() or target.is_symlink():
        target.unlink()
    target.symlink_to(dictionary)
PYOPENCC

# addon 与设置程序的**数据文件**（/usr/share/fcitx5/addon/rime-ai.conf、inputmethod/rime-ai.conf、
# desktop、图标…）走各自的 cmake install 规则 —— 缺了 addon conf，fcitx5 根本不知道有
# 这个输入法：装包不报错，但重启后切不出来。（Ubuntu 线一直是用 install 拿的这些。）
DESTDIR="$PKG" cmake --install "$PRIV/build" --prefix /usr
DESTDIR="$PKG" cmake --install "$PRIV/config-ui/build" --prefix /usr

# addon 描述文件由 src/rime-ai-addon.conf.in.in 生成，装到 ${FCITX_INSTALL_PKGDATADIR}/addon
# —— 那个变量在容器里会解析成 ${PREFIX}/share/fcitx5，而 fcitx5 在 Arch 上只扫
# /usr/share/fcitx5。**这是最初「装完切不出输入法」的根因**：文件在，但位置不对，
# fcitx5 压根不知道有这个 addon。挪到它认的位置，别留 ${PREFIX} 那棵树。
if [ -f "$PKG/usr/local/share/fcitx5/addon/rime-ai.conf" ]; then
  mkdir -p "$PKG/usr/share/fcitx5/addon"
  mv "$PKG/usr/local/share/fcitx5/addon/rime-ai.conf" "$PKG/usr/share/fcitx5/addon/rime-ai.conf"
fi
rm -rf "$PKG/usr/local"

# Keep the renamed settings binary explicit in the package tree.
cp "$PRIV/config-ui/build/bangke-pinyin-config" "$PKG/usr/bin/"

# Private runtime resolution uses the addon's RUNPATH, never ld.so.conf.

bash "$PRIV/dist/lifecycle/stage.sh" "$PKG" omarchy

# 品牌名不在这里做 —— 它直接写在物料里（src/rime-ai.conf.in、src/rime-ai-addon.conf.in.in、
# config-ui/data/bangke-pinyin-config.desktop、metainfo）。以前是打包时 sed 改，但 msgfmt 会拿
# po 展开出 Name[zh_CN]=中州韵 这类本地化键，漏改一个键就漏上游文案出去（Comment 就这么漏过）。

# 数据部分与 Ubuntu 线同源：种子配置来自私有库，词库/字体/模型来自 vendor。
mkdir -p "$PKG/usr/share/fcitx5-rime/seed"
cp -a "$PRIV/data/ai-predict-defaults/"*.custom.yaml "$PKG/usr/share/fcitx5-rime/seed/"
cp -a ${VENDOR}/dicts/.                                "$PKG/usr/share/fcitx5-rime/seed/"
cp -a ${VENDOR}/rime-ice/.                             "$PKG/usr/share/fcitx5-rime/seed/"
mkdir -p "$PKG/usr/share/fcitx5-rime/predict_models"
# 只随包默认那个（出厂种子的 model_path 指向它）。多塞一个模型会让包大近一倍，
# 想试别的模型由设置页的「选择目录…」指过去。
DEFAULT_MODEL="${DEFAULT_MODEL:-zh-base-ct2-int8}"
cp -a "${VENDOR}/models/$DEFAULT_MODEL"                "$PKG/usr/share/fcitx5-rime/predict_models/"
cp -a "$PRIV/data/bangke.yaml"                       "$PKG/usr/share/fcitx5-rime/seed/bangke.yaml"
python3 "$PRIV/dist/lifecycle/opencc-seed.py" "$PKG"
mkdir -p "$PKG/usr/share/fonts/truetype/fcitx5-rime"
cp -a ${VENDOR}/fonts/*.ttf ${VENDOR}/fonts/*.otf "$PKG/usr/share/fonts/truetype/fcitx5-rime/" 2>/dev/null || true

# makepkg 跑在 fakeroot 里，会把文件的 uid/gid 原样记进包 —— 而 /out 是从 Mac 挂进来的，
# 那边的文件在里面显示成 uid 501（Mac 的 rich）。cp -a 会把它带进安装树，装到用户机器上
# 就出现「包内某个目录属主是 501」。统一压回 root:root，别把宿主机的 uid 漏进包。
if [ "$(id -u)" -eq 0 ]; then
  chown -R 0:0 "$PKG"
fi

# ---------------------------------------------------------------- 打 pacman 包
echo "=== [7/7] makepkg ==="
# makepkg 拒绝以 root 跑，建个普通用户来打包。
if [ "$(id -u)" -eq 0 ]; then
  id -u builder >/dev/null 2>&1 || useradd -m builder
fi
BUILD="$OUT/pkgbuild"
rm -rf "$BUILD"; mkdir -p "$BUILD"
cp "$PRIV/dist/arch/PKGBUILD" "$PRIV/dist/arch/bangke-pinyin.install" "$BUILD/"
cp -a "$PKG" "$BUILD/bangke-pkg"
if [ "$(id -u)" -eq 0 ]; then
  chown -R builder "$BUILD"
fi
cd "$BUILD"
# 版本号从 PKGBUILD 读，避免两处写死。
sed -i "s/^pkgver=.*/pkgver=$VERSION/" PKGBUILD
# --nodeps：depends 里是**运行期**依赖（qt5-wayland、librime-data 等），打包容器里
# 不一定装了；它们照常写进包元数据，由目标机器上的 pacman 负责满足。
if [ "$(id -u)" -eq 0 ]; then
  sudo -u builder env PKGEXT='.pkg.tar.zst' makepkg -f --noconfirm --nodeps
else
  PKGEXT='.pkg.tar.zst' makepkg -f --noconfirm --nodeps
fi
# 包挪到 /out 顶层（makepkg 落在它的构建目录里），其余中间产物留在 $BUILD。
mv "$BUILD"/*.pkg.tar.* "$OUT/" 2>/dev/null || true
# 产物交还给挂载目录的主人（容器以 root 写 /out）。中间产物里有 .git 的 pack 文件
# 属于别的用户，chown 会报错 —— 与出包无关，别让它掀掉整个脚本。
chown -R "$(stat -c '%U:%G' "$OUT")" "$OUT" 2>/dev/null || true
ls -lh "$OUT"/*.pkg.tar.*
echo "=== 完成 ==="
