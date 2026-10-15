# Omarchy 候选面板

## 组件

- `panel.py`：GTK3 / Pango / Cairo 绘制，通过标准 kimpanel D-Bus 协议接收候选、翻页和点击操作。蚌壳专用信号提供独立注释与组合输入生命周期。
- `hyprland-caret-plugin/caretshim.cpp`：读取 Hyprland 中的真实输入光标位置，提供面板毛玻璃开关，并禁用候选窗口动画。
- `tray/`：独立启用 Fcitx5 的 StatusNotifierItem，保留托盘和设置入口。
- `themes/`：23 个 Omarchy 配色定义及原始许可证。系统和用户安装的同名主题优先于内置目录。

## 布局与绘制

组合输入期间固定面板原点和外层画布，AI 候选更新不重新定位窗口。Pango 使用完整文本换行，超长内容可以滚动；不截断候选、不缩小字体。首尾高亮继承面板外侧圆角，中间相邻边界保持连续。

背景、选中背景与阴影合成后统一应用背景透明度；文字和边框保持清晰。面板先清空上一帧，避免半透明背景反复叠加。

## 外观与主题

Qt 解析 YAML 并输出版本化外观 JSON，供设置预览与运行面板共用。配置文件是用户 Rime 目录中的 `bangke.yaml` 和 `bangke.custom.yaml`，运行缓存位于 `$XDG_CONFIG_HOME/bangke-panel/`。

`native` 表示跟随系统主题；`omarchy_<name>` 固定使用指定主题。监听 state/config 下的 Omarchy 当前主题目录，合并切换事件并拒绝过期的异步结果。用户已选择的固定配色不会在升级时被覆盖。

## 安装与生命周期

统一通过 `dist/build-arch.sh` 打包并由 pacman 安装，不再支持旧实验安装脚本。`dist/lifecycle/` 管理用户注册、种子数据同步、服务及卸载。`migrate-experiment.py` 是必要的升级兼容代码，只回收已知旧实验启动项，不能当临时诊断脚本删除。

服务为 `bangke-panel.service`、`bangke-caret.service` 和 `bangke-session.service`。Hyprland 插件在用户机器使用匹配的头文件编译，加载前检查 ABI；升级 Hyprland 后需要重新登录。已验证版本为 Hyprland 0.56.2。

## 验证

`test-appearance.py` 检查真实绘制像素及完整长文本；`test-stability.py` 检查组合输入定位和布局；`test-live-settings.py`、`test-theme-switch.py` 使用独立 D-Bus 与 Xvfb 检查真实 GTK / Qt 协作；`tray/test-registration.py` 检查标准托盘注册。`render-reference.py` 可生成六种高亮参考图，产物不提交。

完整命令见 [LINUX_BUILD.md](../LINUX_BUILD.md)。运行时问题首先检查 `journalctl --user -u bangke-panel -u bangke-caret` 和 `~/.cache/bangke-pinyin/setup.log`。
