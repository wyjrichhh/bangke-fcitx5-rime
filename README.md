# 蚌壳拼音 Linux 前端

基于 [fcitx/fcitx5-rime](https://github.com/fcitx/fcitx5-rime) 的 Linux 输入法分支，提供本地 AI 候选、Qt 设置界面和桌面专用候选面板。长期维护和打包入口统一为 **`main`**，ARM 与 Intel/AMD 共用源码；Omarchy 与 Ubuntu 的平台差异由构建和运行时组件处理。

基础 Fcitx5 / Rime 功能见[上游项目](https://github.com/fcitx/fcitx5-rime#readme)。本仓库主要维护以下扩展。

## 我们扩展的能力与机制

### 本地 AI 与异步候选

接入 [bangke-ai-predict](https://github.com/wyjrichhh/bangke-ai-predict)，以已提交上文和当前拼音生成候选。模型推理在本机后台运行，前端根据 `ai_predict/pending` 状态在自身事件循环推动组合更新，后台工作线程不并发执行 Rime 的组合流水线。

addon 使用 `rime-ai` 注册名，避免覆盖发行版 `rime` addon 的模块与注册文件。Rime 用户数据仍使用 Fcitx5 的 `rime` 目录，配置并非完全隔离。

### Omarchy 独立面板

`panel-omarchy/` 使用 GTK3 / Pango / Cairo 和 layer-shell 绘制独立候选窗口，经蚌壳专属 D-Bus 通道接收候选、翻页和点击操作，通过蚌壳专用信号传递独立注释与组合输入生命周期。

组合期间固定面板原点，AI 更新不反复移动窗口；长候选按完整文本换行、滚动。背景透明度与文字绘制分开处理。Hyprland 插件读取真实输入光标位置，控制模糊与窗口动画，并在加载前检查 ABI。独立托盘组件保留设置及菜单入口。

### 统一设置和主题协议

Qt5 设置程序解析 `bangke.yaml` / `bangke.custom.yaml`，输出版本化外观 JSON，供设置预览与运行面板共同使用。Omarchy 主题跟随会监听系统主题切换，合并事件并拒绝过期异步结果；固定配色在升级时保留。

Ubuntu GNOME 面板复用同一套外观和候选协议，使用 GNOME Shell 的 St / Clutter 绘制，具体实现与验收限制见 [panel-gnome/README.md](panel-gnome/README.md)。

### 安装、升级与更新检查

`dist/lifecycle/` 统一处理用户注册、种子数据、旧实验版本迁移和服务生命周期。Omarchy 通过 systemd 用户服务启动面板、光标集成与会话初始化。设置程序提供更新检查，用户服务运行更新通知监控。

打包脚本分别装配 addon、引擎、AI 插件、模型和平台组件；`dist/build-arch.sh` 面向 Omarchy 的两种架构，Ubuntu 使用独立构建入口。

## 平台与安装

- **Omarchy / Hyprland**：`aarch64` 与 `x86_64` Arch 包，使用独立 GTK layer-shell 面板，不与其他输入法共享蚌壳配色。
- **Ubuntu 24.04 amd64 / GNOME**：新增 GNOME Shell 46 面板实验实现，共用外观配置；已通过独立 Wayland 合成器和真实 GTK 输入测试，完整发布验收待完成；其他桌面保留 Classic UI。

Omarchy 安装或升级（将占位符替换为实际下载的文件名）：

```bash
sudo pacman -U "./bangke-pinyin-<版本>-<pkgrel>-<架构>.pkg.tar.zst"
```

首次安装后注销并重新登录。托盘可打开设置。「外观 → 配色方案 → 跟随 Omarchy 系统主题」会自动响应主题切换；也可固定选择一个内置或本机主题。升级保留用户选择、词库与配置。

## 开发入口

- [构建与验证](LINUX_BUILD.md)：依赖、三种包、回归测试与发布要求。
- [Omarchy 架构](panel-omarchy/README.md)：候选协议、定位、主题和服务。
- [安装与故障排查](INSTALL_FAQ.md)。

`src/` 是 Fcitx5 addon，`config-ui/` 是 Qt5 设置程序，`panel-omarchy/` 是 GTK 面板、Hyprland 插件及托盘 addon，`dist/` 负责安装、升级、卸载和打包。

开发和打包需同时取得 [bangke-librime](https://github.com/wyjrichhh/bangke-librime)、[bangke-ai-predict](https://github.com/wyjrichhh/bangke-ai-predict) 及发布物料。模型、词典、字体、第三方源码和编译产物不提交到此仓库。

## 验收基线与限制

Omarchy `5.0.19-10` 已在 ARM 虚拟机验收定位、圆角、高亮、完整长文本、主题跟随和背景透明度。Intel 包通过编译、容器安装与回归检查，仍需真实 Intel/AMD Omarchy 桌面验收。分支整理后的提交以此行为为基线；原安装包由共享目录中的校验文件标识。

## 上游与许可

感谢 [fcitx/fcitx5-rime](https://github.com/fcitx/fcitx5-rime)、[rime/librime](https://github.com/rime/librime) 和[雾凇拼音](https://github.com/iDvel/rime-ice) 的贡献者。源码保留原有版权声明，许可证见 [LICENSES](LICENSES)；AI 插件、主题和模型遵循各自许可。

Ubuntu GNOME 面板的实现、限制与验收步骤见 [panel-gnome/README.md](panel-gnome/README.md)。
