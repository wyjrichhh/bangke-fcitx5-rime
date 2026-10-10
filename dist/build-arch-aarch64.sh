#!/usr/bin/env bash
# 蚌壳拼音 —— Arch Linux ARM (aarch64) 构建脚本。在 Arch 容器里跑：
#
#   docker run --rm -v <私有库>:/priv -v <第三方产物区>:/src \
#     menci/archlinuxarm:base bash /priv/dist/build-arch-aarch64.sh
#
# 产物在 /priv/build-out/bangke-arch-aarch64/（可直接 sudo 那个目录里的安装脚本）。
#
# 与 build-22.04.sh 的关系：22.04 那条线是给 Ubuntu 用户的基线包，两条线产物互不通用
# （glibc 方向相反：Arch 是滚动最新）。这里是 aarch64 + Arch 的独立平台。
set -euo pipefail

PRIV="${PRIV:-/priv}"
SRC="${SRC:-/src}"
OUT="$PRIV/build-out/bangke-arch-aarch64"

# ---------------------------------------------------------------- 依赖
# 运行期和构建期的包在 Arch 上是同一份（头文件随包走），所以只用一组。
echo "=== [1/6] 安装依赖 ==="
# 容器里的 pacman 下载沙箱要 Landlock，某些内核上不可用；失败就关掉重来。
pacman -Syu --noconfirm || { sed -i '/^\[options\]/a DisableSandbox' /etc/pacman.conf; pacman -Syu --noconfirm; }
# --needed 重试：镜像偶尔超时，多跑几轮补齐（`Operation too slow` 不算依赖缺失）。
for _ in 1 2 3; do
  pacman -S --needed --noconfirm \
    base-devel cmake ninja git extra-cmake-modules gettext pkgconf \
    fcitx5 qt5-base boost google-glog yaml-cpp gflags leveldb marisa opencc capnproto lua \
    && break
done

# Arch 是滚动发行版，cmake 4.x 已移除对 cmake_minimum_required(<3.5) 的兼容，
# 而我们和几个第三方工程的 CMakeLists 都还写着老版本号 —— 统一用这个开关放行，
# 不改上游源码。
CMAKE_OLD_POLICY=(-DCMAKE_POLICY_VERSION_MINIMUM=3.5)

# ---------------------------------------------------------------- Ruy 的 cpuinfo
# CTranslate2 的 Ruy 后端需要 ruy/third_party/cpuinfo；Mac 工作树里这个子模块通常是空的，
# 缺失时 ruy 报 "target cpuinfo::cpuinfo not found"，编到 Generate 阶段才炸。
CPUINFO="$SRC/ctranslate2-src/third_party/ruy/third_party/cpuinfo"
if [ ! -f "$CPUINFO/CMakeLists.txt" ]; then
  echo "=== 取 ruy 的 cpuinfo 子模块 ==="
  git clone --depth 1 https://github.com/pytorch/cpuinfo.git "$CPUINFO.tmp"
  rm -rf "$CPUINFO" "$CPUINFO.tmp/.git"
  mv "$CPUINFO.tmp" "$CPUINFO"
fi

# ---------------------------------------------------------------- CTranslate2
# **必须是共享库**。deps.mk 里写的是 BUILD_SHARED_LIBS=OFF（那是 macOS 侧的用法），
# 但 Linux 产品线的插件链的是 libctranslate2.so.4（ruy 编在里面）。照 deps.mk 编成静态，
# 插件就会留一堆未定义的 ruy 符号（`undefined symbol: ruy::Context::~Context()`），
# 表现是 addon 能加载、AI 预测静默失效。RPATH 直接指向 bundle 目录，和 deb 一致。
echo "=== [2/6] 构建 CTranslate2 (共享, Ruy 后端) ==="
cd "$SRC/ctranslate2-src"
rm -rf build
cmake . -Bbuild -G Ninja -DBUILD_SHARED_LIBS=ON -DBUILD_CLI=OFF -DBUILD_TESTS=OFF \
  -DWITH_CUDA=OFF -DWITH_MKL=OFF -DWITH_OPENBLAS=OFF -DWITH_RUY=ON -DWITH_ACCELERATE=OFF \
  -DOPENMP_RUNTIME=NONE -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DCMAKE_INSTALL_RPATH=/usr/lib/fcitx5-rime-ai \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON "${CMAKE_OLD_POLICY[@]}"
cmake --build build
cmake --build build --target install

# ---------------------------------------------------------------- librime + 插件
echo "=== [3/6] 构建 librime + librime-ai-predict ==="
# librime 的 plugins/ 是个空 submodule，得把独立的插件工作树放进去。
rm -rf "$SRC/librime/plugins/librime-ai-predict"
mkdir -p "$SRC/librime/plugins/librime-ai-predict"
cp -a "$SRC/librime-ai-predict/." "$SRC/librime/plugins/librime-ai-predict/"
cd "$SRC/librime"
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=ON -DBUILD_MERGED_PLUGINS=OFF -DENABLE_EXTERNAL_PLUGINS=ON \
  -DBUILD_TEST=OFF -DBUILD_SAMPLE=OFF -DCTRANSLATE2_ROOT=/usr/local "${CMAKE_OLD_POLICY[@]}"
ninja -C build -j "$(nproc)"

# 插件里不该残留 ruy 符号 —— 留着就是上面那个静默失效。编完必须查。
SO="$SRC/librime/build/lib/rime-plugins/librime-ai-predict.so"
RUY_UNDEF=$(nm -D --undefined-only "$SO" | grep -c ruy || true)
[ "$RUY_UNDEF" -eq 0 ] || { echo "插件残留 $RUY_UNDEF 个未定义 ruy 符号，AI 预测会失效" >&2; exit 1; }

# ---------------------------------------------------------------- addon
echo "=== [4/6] 构建 fcitx5-rime addon ==="
cd "$PRIV"
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/usr/local -DRIME_DATA_DIR=/usr/share/rime-data "${CMAKE_OLD_POLICY[@]}"
ninja -C build -j "$(nproc)"

# ---------------------------------------------------------------- 设置程序
echo "=== [5/6] 构建 config-ui (Qt5) ==="
rm -rf "$PRIV/config-ui/build"
cmake -S "$PRIV/config-ui" -B "$PRIV/config-ui/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr "${CMAKE_OLD_POLICY[@]}"
ninja -C "$PRIV/config-ui/build" -j "$(nproc)"

# ---------------------------------------------------------------- 组包
echo "=== [6/6] 组包 → $OUT ==="
# 数据部分（seed / 模型 / conf / desktop）直接取 22.04 那条线建的 deb —— 平台无关，
# 只有二进制换成本次的 aarch64。所以这里需要 /src/deb-resources 或已有的 deb。
rm -rf "$OUT"
mkdir -p "$OUT"/{usr/lib/fcitx5,usr/lib/fcitx5-rime-ai/rime-plugins,usr/bin,etc/ld.so.conf.d,usr/share/fcitx5/{addon,inputmethod},usr/share/applications}

cp build/src/rime.so                              "$OUT/usr/lib/fcitx5/rime.so"
cp /usr/local/lib/libctranslate2.so.*             "$OUT/usr/lib/fcitx5-rime-ai/"
cp "$SRC/librime/build/lib/librime.so.1.17.0"     "$OUT/usr/lib/fcitx5-rime-ai/"
ln -sf librime.so.1.17.0                          "$OUT/usr/lib/fcitx5-rime-ai/librime.so.1"
ln -sf librime.so.1                               "$OUT/usr/lib/fcitx5-rime-ai/librime.so"
cp "$SRC/librime/build/lib/rime-plugins/librime-ai-predict.so" "$OUT/usr/lib/fcitx5-rime-ai/rime-plugins/"
cp "$PRIV/config-ui/build/fcitx5-rime-config"     "$OUT/usr/bin/"

# Arch 的库目录是 /usr/lib；addon 的 conf 也必须落在 /usr/share/fcitx5/addon ——
# deb 用的 /usr/local/share 在 Arch 上**扫不到**（实测：fcitx5-diagnose 数不到 Rime）。
echo /usr/lib/fcitx5-rime-ai > "$OUT/etc/ld.so.conf.d/fcitx5-rime-ai.conf"

DEB_SRC="$SRC/deb-resources"
[ -d "$DEB_SRC" ] || { echo "缺 $DEB_SRC（seed/模型/conf 的来源）" >&2; exit 1; }
cp -a "$DEB_SRC/usr/share/fcitx5-rime"            "$OUT/usr/share/"
cp "$DEB_SRC"/usr/local/share/fcitx5/addon/rime.conf "$OUT/usr/share/fcitx5/addon/rime.conf"
cp "$DEB_SRC"/usr/share/fcitx5/inputmethod/rime.conf "$OUT/usr/share/fcitx5/inputmethod/rime.conf"
cp "$DEB_SRC"/usr/share/applications/fcitx5-rime-config.desktop "$OUT/usr/share/applications/"
cp "$PRIV/dist/install-arch-aarch64.sh"           "$OUT/"
chmod +x "$OUT/install-arch-aarch64.sh"

# 字体：deb 里带了两个中文字体，是给没有 CJK 字体的系统兜底的，别漏。
cp -a "$DEB_SRC"/usr/share/fonts "$OUT/usr/share/" 2>/dev/null || \
  echo "注意：$DEB_SRC 里没有 fonts/，面板字体只能靠系统兜底" >&2

echo "=== 完成 ==="
ls -l "$OUT/usr/lib/fcitx5/rime.so" "$OUT/usr/lib/fcitx5-rime-ai/rime-plugins/librime-ai-predict.so"
du -sh "$OUT"

# ---------------------------------------------------------------- 打 tar
# **COPYFILE_DISABLE=1 不能少**：macOS 的 bsdtar 默认把扩展属性存成 AppleDouble
# `._*` 条目，在 Linux 上解包会变成真实文件。轻则 librime 把 `._librime-ai-predict.so`
# 当插件去加载、日志刷 "无效的 ELF 头"，重则 rime 把 `._*.yaml` 当配置解析。
echo "=== 打包 ==="
TAR="$PRIV/build-out/bangke-arch-aarch64.tar.zst"
rm -f "$TAR"
( cd "$(dirname "$OUT")" && COPYFILE_DISABLE=1 tar --zstd -cf "$TAR" "$(basename "$OUT")" )
tar --zstd -tf "$TAR" | grep -q '/\._' && { echo "包里混进了 ._ 条目，检查 COPYFILE_DISABLE" >&2; exit 1; }
ls -lh "$TAR"
