# Linux 构建与验证

## 源码与物料

维护分支：`feat/omarchy-panel`。从私有仓库克隆本项目，并准备 `bangke-librime`、`bangke-ai-predict`。发布模型、字体、词典和第三方源码从团队物料库取得，不依赖开发者共享目录或 SSH 主机。

容器挂载约定：

| 路径 | 内容 |
| --- | --- |
| `/priv` | 本仓库，可写；构建目录不能跨架构或发行版共享 |
| `/librime` | 蚌壳 Rime 引擎源码 |
| `/plugin` | AI prediction 插件源码 |
| `/vendor` | 第三方与发布物料 |
| `/out` | 当前目标平台的独立构建输出 |

`/vendor` 需要 `ctranslate2/`（含所有 submodule）、`librime-lua/`、`dicts/`、`rime-ice/`、`models/` 和 `fonts/`。模型目录及名称以打包脚本中的 `DEFAULT_MODEL` 为准。`dist/stage-vendor.sh` 是旧物料迁移工具，不是日常构建前置步骤。

## Omarchy：ARM 与 Intel/AMD

两种架构运行同一脚本，各自使用对应 Arch 用户空间；Mac 上的 Intel 构建需要 Docker 支持 amd64 模拟。

```bash
# 从本仓库根目录运行。按实际位置修改三个物料路径。
mkdir -p build-out/aarch64
docker run --rm --platform linux/arm64 \
  -v "$PWD:/priv" -v /path/to/bangke-librime:/librime:ro \
  -v /path/to/bangke-ai-predict:/plugin:ro -v /path/to/vendor:/vendor:ro \
  -v "$PWD/build-out/aarch64:/out" \
  menci/archlinuxarm:base bash /priv/dist/build-arch.sh
```

Intel/AMD 将 `--platform` 改为 `linux/amd64`，镜像改为 `archlinux:latest`，输出目录改为 `build-out/x86_64`。两个构建不要同时挂载同一份可写源码：使用独立克隆或 worktree，防止 `build/` 和 `config-ui/build/` 混入另一架构的对象文件。

结果为 `/out/bangke-pinyin-<版本>-<pkgrel>-<架构>.pkg.tar.zst`。版本默认 `5.0.19`，当前 pkgrel 为 `10`。`SKIP_DEPS=1`、`SKIP_LIBRIME=1` 只适用于已有且匹配的构建环境。`dist/repack-arch.sh` 复用已组装的二进制，源码变更后必须先重新编译并更新安装树，不能用重打包代替构建。

## Ubuntu 24.04 amd64

在 `ubuntu:24.04` 容器中使用同样挂载约定，输出到独立的 `/out`，执行：

```bash
bash /priv/dist/build-ubuntu24.sh
```

保留 Classic UI；不把 Hyprland 插件或 Omarchy 主题能力作为 GNOME 支持。`build-22.04.sh` 是共用的 Debian 系编译实现，外层入口负责目标系统检查。

## 回归检查

Linux 环境需要 Qt5 开发包、Python GTK3/Pango introspection、Cairo、D-Bus、Xvfb 和中文字体。先构建设置程序及相关测试：

```bash
cmake -S config-ui -B config-ui/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build config-ui/build --target fcitx5-rime-config configuration_test \
  platformtheme_test custompanel_save_test
QT_QPA_PLATFORM=offscreen BK_OMARCHY_CATALOG_DIR="$PWD/panel-omarchy/themes" \
  config-ui/build/configuration_test
QT_QPA_PLATFORM=offscreen BK_OMARCHY_CATALOG_DIR="$PWD/panel-omarchy/themes" \
  config-ui/build/platformtheme_test
python3 panel-omarchy/test-appearance.py
python3 panel-omarchy/test-stability.py
python3 dist/lifecycle/test-user-data.py
dbus-run-session -- python3 panel-omarchy/test-live-settings.py
dbus-run-session -- python3 panel-omarchy/test-theme-switch.py
```

D-Bus 测试使用私有 HOME；Xvfb 测试使用显示号 `:97`，不要并发执行。托盘测试需要先安装对应架构的完整包：`dbus-run-session -- python3 panel-omarchy/tray/test-registration.py`。生成视觉参考：`python3 panel-omarchy/render-reference.py /tmp/bangke-references`。

发布前在两种架构检查安装、重复安装和升级；真实桌面验收输入定位、长文本、主题切换、透明度、毛玻璃及托盘。记录构建提交、引擎和 AI 插件提交、物料版本及包 SHA256。不要把容器测试等同于真实桌面验收。
