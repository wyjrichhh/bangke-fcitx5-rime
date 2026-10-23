# Linux 构建与验证

## 源码与物料

维护分支：`main`。从项目仓库克隆本项目，并准备 `bangke-librime`、`bangke-ai-predict`。发布模型、字体、词典和第三方源码从团队物料库取得，不依赖开发者共享目录或 SSH 主机。

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

结果为 `/out/bangke-pinyin-<版本>-<pkgrel>-<架构>.pkg.tar.zst`。版本默认 `5.0.19`，当前 pkgrel 为 `23`。`SKIP_DEPS=1`、`SKIP_LIBRIME=1` 只适用于已有且匹配的构建环境。`dist/repack-arch.sh` 复用已组装的二进制，源码变更后必须先重新编译并更新安装树，不能用重打包代替构建。

## Ubuntu 24.04 amd64

在 `ubuntu:24.04` 容器中使用同样挂载约定，输出到独立的 `/out`，执行：

```bash
bash /priv/dist/build-ubuntu24.sh
```

包中包含 GNOME Shell 46 面板扩展实验实现，不安装 Hyprland；未启用扩展时保留 Classic UI。实现与真实桌面验收要求见 [panel-gnome/README.md](panel-gnome/README.md)。`build-22.04.sh` 是共用的 Debian 系编译实现，外层入口负责目标系统检查。

## 回归检查

Linux 环境需要 Qt5 开发包、Python GTK3/Pango introspection、Cairo、D-Bus、Xvfb 和中文字体。先构建设置程序及相关测试：

```bash
cmake -S config-ui -B config-ui/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build config-ui/build --target bangke-pinyin-config configuration_test \
  platformtheme_test custompanel_save_test layoutpreview_test
QT_QPA_PLATFORM=offscreen BK_OMARCHY_CATALOG_DIR="$PWD/panel-omarchy/themes" \
  config-ui/build/configuration_test
QT_QPA_PLATFORM=offscreen BK_OMARCHY_CATALOG_DIR="$PWD/panel-omarchy/themes" \
  config-ui/build/platformtheme_test
QT_QPA_PLATFORM=offscreen BK_OMARCHY_CATALOG_DIR="$PWD/panel-omarchy/themes" \
  config-ui/build/layoutpreview_test
python3 panel-omarchy/test-appearance.py
python3 panel-omarchy/test-stability.py
python3 dist/lifecycle/test-user-data.py
dbus-run-session -- python3 panel-omarchy/test-live-settings.py
dbus-run-session -- python3 panel-omarchy/test-theme-switch.py
```

D-Bus 测试使用私有 HOME；Xvfb 测试使用显示号 `:97`，不要并发执行。托盘测试需要先安装对应架构的完整包：`dbus-run-session -- python3 panel-omarchy/tray/test-registration.py`。生成视觉参考：`python3 panel-omarchy/render-reference.py /tmp/bangke-references`。

Omarchy 真实桌面还需验证面板隔离（需要 `xorg-xwininfo`）：`DISPLAY=:1 python3 panel-omarchy/test-isolation.py`，显示号使用该会话的实际值。该测试确认原生/蚌壳窗口归属、方案切换气泡及超时、推理期间快速切换、点击翻页、失焦和面板停止/恢复，不通过客户端候选绕过真实 UI。`test-live-settings.py` 和 `test-theme-switch.py` 可设置 `BANGKE_TEST_DISPLAY` 复用现有 XWayland，仍需通过 `dbus-run-session` 隔离总线。

发布前在两种架构检查安装、重复安装和升级；真实桌面验收输入定位、长文本、主题切换、透明度、毛玻璃及托盘。记录构建提交、引擎和 AI 插件提交、物料版本及包 SHA256。不要把容器测试等同于真实桌面验收。

## 设置布局与预览覆盖

设置分类在顶部独立居中；外观、行为和 AI 页采用左侧独立滚动设置、右侧固定位置的紧凑预览。右侧预览卡片固定高度，不随设置数量、展开高级选项或候选内容增长而拉高。外观页默认展示常用设置，其余参数集中在「更多外观选项」中；普通与长句示例可以切换。候选预览使用当前系统主题，保持实际字号，内容超出时滚动查看，不整体缩小。设置窗口始终使用系统主题；候选配色仍保留已有的跟随系统与固定方案选择能力。

`layoutpreview_test` 在 Tokyo Night 和 Flexoki Light 下操作真实控件，比较候选画布像素及输入场景文字，检查导航居中、切页、设置滚动与预览位置。可用 `BANGKE_LAYOUT_SCREENSHOTS=/tmp/bangke-layout` 保存整窗离屏参考；参考图不能代替真实 Wayland 桌面验收。

| 设置 | 预览范围与条件 |
| --- | --- |
| 配色、字体、字号、背景透明度、圆角、阴影、边框宽高、候选排列、文字方向、行间距、候选格式 | 联动候选画布；只显示当前深浅模式对应的配色。边框宽高取较小值，任何一项为 0 时不描边。 |
| 预编辑位置、间距 | 关闭输入框内预编辑后，拼音出现在候选面板顶部，此时间距才可见。 |
| 输入框内选中候选 | 输入框内预编辑开启时，在上方输入场景切换拼音与选中候选。 |
| 翻页按钮 | 联动候选窗箭头的显示；真实输入还受候选总数影响，预览不模拟翻页请求。 |
| 避免透明颜色叠加 | 联动背景与高亮的合成；底色完全不透明时，两种合成可能视觉相同。 |
| 每页候选数、拼音提示 | 行为页联动示例候选数量与注释；不调用真实词典。 |
| 高亮圆角 | 当前贴边连续高亮将内部边界固定为直角，此项保留禁用状态，不声称预览或实际形状生效。 |
| 毛玻璃背景 | 静态画布不能验证 Hyprland 背景采样；需要实际合成器及非零背景透明度。 |
| 记忆本次输入的面板宽度 | 需要连续输入、候选更新及结束输入的完整生命周期，静态预览不模拟。 |
| Shift 切换动作 | 需要真实键盘事件与 Fcitx5 上下文，不在候选画布模拟。 |
| AI 启用、候选插入位置、过滤标点 | AI 页联动固定示例，不运行模型；插入位置超出当前页时，该页不显示 AI 示例。 |
| AI 模型与触发、生成、去重参数；方案启用、简繁转换、用户词典 | 需要真实模型或输入方案运行结果；不能用静态候选示例替代验证。 |

预览在设置区域内滚动展示完整长文本；运行面板根据真实屏幕可用宽度换行，两者的可用宽度不同，不作逐像素一致承诺。Qt 与 GTK/Pango 的字体栅格化差异仍需实机对照。Wayland 桌面优先使用原生 Qt Wayland 后端，以使用合成器提供的屏幕缩放；显式 `BK_QT_PLATFORM=xcb` 仍可用于 XWayland 对照。
