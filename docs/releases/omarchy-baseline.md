# Omarchy 维护基线

本分支从蚌壳 `feat/arch-aarch64` 的 `f4a0d3a` 建立，迁入用户已验收的 Omarchy `5.0.19-10` 实现。此前实验 worktree 从上游 `3509646` 创建，未形成开发提交；本次集中整理为可协作的维护基线。

已验收能力包括独立候选面板、精确光标定位、首尾圆角高亮、完整长文本和稳定布局、托盘与 Qt 设置、23 个系统配色与主题跟随，以及完整背景层透明度。

整理时移除了临时安装/诊断脚本、实验笔记和生成截图；保留回归测试、发布脚本及旧安装迁移。面板逐键日志改为显式启用 `BANGKE_PANEL_DEBUG=1`，设置保存日志移到用户缓存目录。因此此维护提交是经过清理的源码基线，不声称与此前已分发安装包逐字节相同。

此前验收包的 SHA256：

```text
837d7d188b417d29bec13c134c488b25271fa0211a3bdd3d3e3ec78f76418939  bangke-pinyin-5.0.19-10-aarch64.pkg.tar.zst
4379540ea10523a4f48daf95ec8ed30134ed2e5d6ba8eae8e9b2b668e1e4c672  bangke-pinyin-5.0.19-10-x86_64.pkg.tar.zst
```

ARM 包已完成用户虚拟机验收。Intel 包通过构建、容器安装与回归，真实 Intel/AMD 桌面验收仍需补充。Ubuntu 包保持原有 GNOME Classic UI 路线，不能将 Omarchy 验收视为 Ubuntu 自绘面板支持。
