# 蚌壳拼音 Linux 前端

基于 Fcitx5 / Rime 的蚌壳拼音前端，支持本地 AI 候选、Qt 设置界面和 Omarchy 自绘候选面板。长期维护分支为 `feat/omarchy-panel`，ARM 与 Intel/AMD 共用源码。

## 平台与安装

- **Omarchy / Hyprland**：`aarch64` 与 `x86_64` Arch 包，使用独立 GTK layer-shell 面板，不与其他输入法共享蚌壳配色。
- **Ubuntu 24.04 amd64 / GNOME**：新增 GNOME Shell 46 面板实验实现，共用外观配置；已通过独立 Wayland 合成器和真实 GTK 输入测试，完整发布验收待完成；其他桌面保留 Classic UI。

Omarchy 安装或升级：

```bash
sudo pacman -U ./bangke-pinyin-5.0.19-10-$(uname -m).pkg.tar.zst
```

首次安装后注销并重新登录。托盘可打开设置。「外观 → 配色方案 → 跟随 Omarchy 系统主题」会自动响应主题切换；也可固定选择一个内置或本机主题。升级保留用户选择、词库与配置。

## 开发入口

- [构建与验证](LINUX_BUILD.md)：依赖、三种包、回归测试与发布要求。
- [Omarchy 架构](panel-omarchy/README.md)：候选协议、定位、主题和服务。
- [安装与故障排查](INSTALL_FAQ.md)。

`src/` 是 Fcitx5 addon，`config-ui/` 是 Qt5 设置程序，`panel-omarchy/` 是 GTK 面板、Hyprland 插件及托盘 addon，`dist/` 负责安装、升级、卸载和打包。

开发此分支需同时取得私有 `bangke-librime`、`bangke-ai-predict` 及发布物料。模型、词典、字体、第三方源码和编译产物不提交到此仓库。

## 验收基线

Omarchy `5.0.19-10` 已在 ARM 虚拟机验收定位、圆角、高亮、完整长文本、主题跟随和背景透明度。Intel 包通过编译、容器安装与回归检查，仍需真实 Intel/AMD Omarchy 桌面验收。分支整理后的提交以此行为为基线；原安装包由共享目录中的校验文件标识。

上游项目：[fcitx/fcitx5-rime](https://github.com/fcitx/fcitx5-rime)。许可证见 [LICENSES](LICENSES)。

Ubuntu GNOME 面板的实现、限制与验收步骤见 [panel-gnome/README.md](panel-gnome/README.md)。
