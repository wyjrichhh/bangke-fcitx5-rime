# Ubuntu GNOME 候选面板（实验实现）

目标环境是 Ubuntu 24.04 / GNOME Shell 46。已在 Ubuntu 24.04.4 / GNOME 46 的独立硬件加速 Wayland 合成器中验证绘制、输入和设置保存；物理桌面多屏、混合缩放等完整发布验收仍待完成。其他 GNOME 版本不在扩展 metadata 中声明，非 GNOME 桌面继续使用已有 UI。

与 Hyprland 共用 Fcitx5/Rime/AI addon、标准 Kimpanel D-Bus、`org.bangke.Candidate1` 的独立注释和组合输入信号，以及 Qt 从 YAML 输出的 version 1 外观 JSON。颜色、字体、透明度、圆角、候选格式和排列均来自同一份配置；不复制配置解析器或 AI 逻辑。GTK 与 GNOME Shell 的窗口、绘制后端分别维护。

## 实现

- `bangke-panel@bangke.org/extension.js`：GNOME Shell 内的候选面板。使用 St/Clutter 承载候选，通过 Clutter 克隆桌面窗口内容，再使用 Shell 的 ACTOR 模糊；背景与文字分开处理，不需要安装 Hyprland。
- `rounded.js`：裁剪合成后的圆角，覆盖模糊背景；已通过 AMD GPU 的真实合成器截图与背景边缘能量对比验证。
- `model.js`：外观协议校验、候选角色、组合输入原点和屏幕边界布局。
- `../dist/lifecycle/gnome.sh`：通过已有设置程序生成外观缓存，启用扩展。其他 Kimpanel 正在运行时保留它，不抢占总线。卸载只撤销安装流程创建的启用记录，保留用户数据。

扩展安装到 `/usr/share/gnome-shell/extensions/bangke-panel@bangke.org/`。GNOME 首次看到新系统扩展需要注销、重新登录；初始化程序在扩展可用后启用它。Fcitx5 通过 Kimpanel 可用性切换面板，Classic UI addon 保持可用。没有改动 GDM、桌面会话或用户的 Hyprland 配置。

外观缓存为 `$XDG_CONFIG_HOME/bangke-panel/appearance.json`；设置保存走现有 `GetPanelIdentity` / `SetAppearance` 接口，返回 `applied:1`。监听外观缓存和 GNOME 深浅色切换；注销登录重新从 YAML 生成缓存。固定 Omarchy 配色也随 Ubuntu 包分发。

目前 GNOME 实现支持候选横排/堆叠、文字换行和滚动、鼠标选词与翻页、背景透明度、圆角、背景模糊、边框、高亮阴影及组合输入原点固定。预编辑与输入框内候选选项沿用已有 addon 设置。

## 已知限制与验收重点

- 逐字纵向排版以直立字形逐字排列，候选列表方向可独立设置；复杂标点旋转尚未专门适配。
- 「避免透明颜色叠加」通过独立背景遮罩移除高亮区域的面板底色。
- 已验证 AMD GPU / 100% 缩放的圆角模糊、长候选、组合输入定位与 GTK Wayland 中文提交。其他显卡、混合缩放、多屏、长候选自动滚动至选中项仍需验证。
- 没有接管其他 Kimpanel 的输入法菜单；已有托盘/输入法切换方式继续承担这一功能。
- Shell 扩展运行在桌面进程中；发布前必须确认 enable/disable、注销登录和崩溃日志，不能用 Node 测试替代 Shell 验收。

## 本地回归

需要 Python 3、Bash 4+ 和 Node 18+，无需安装 npm 依赖：

```bash
node --test panel-gnome/test-model.mjs
node --check panel-gnome/bangke-panel@bangke.org/extension.js
node --check panel-gnome/bangke-panel@bangke.org/rounded.js
python3 panel-gnome/test-install.py
python3 dist/lifecycle/test-user-data.py
bash -n dist/lifecycle/*.sh dist/repack-container.sh
```

## Ubuntu 构建与桌面验收

完整依赖和 Docker 挂载约定见 `../LINUX_BUILD.md`，Ubuntu 包仍使用 `dist/build-ubuntu24.sh`；必须编译当前 worktree 中的 addon 和 Qt 设置程序。

在测试机上为此分支创建独立源码和输出目录，不覆盖其他分支的构建树。例如把 worktree 同步到 `/home/rich/bangke/ubuntu-gnome-panel/fcitx5-rime`，引擎、插件、vendor 使用已核实的本机物料路径：

```bash
docker run --rm \
  -v /home/rich/bangke/ubuntu-gnome-panel/fcitx5-rime:/priv \
  -v /实际位置/bangke-librime:/librime:ro \
  -v /实际位置/bangke-ai-predict:/plugin:ro \
  -v /实际位置/vendor:/vendor:ro \
  -v /home/rich/bangke/ubuntu-gnome-panel/out:/out \
  ubuntu:24.04 bash /priv/dist/build-ubuntu24.sh
```

通过 apt 安装新构建的 deb。首次安装后注销、登录，在该用户桌面终端运行：

```bash
gnome-shell --version
gnome-extensions info bangke-panel@bangke.org
python3 panel-gnome/test-desktop.py
journalctl --user -b /usr/bin/gnome-shell --no-pager
```

`test-desktop.py` 校验真实总线身份、外观往返、布局更新并恢复原设置；需先在设置界面保存一次外观。

随后分别在 GTK、Qt、浏览器、终端与 XWayland 应用中输入中文，检查光标定位、AI 异步更新、长文本、鼠标选词、翻页、浅深色、背景透明度和毛玻璃开关。覆盖 100%/200%/混合缩放、多显示器、屏幕下沿、锁屏和概览。保存对比截图，检查日志后再判断是否达到 Omarchy 的视觉效果。

最后验证扩展禁用后的 Classic UI 回退、其他 Kimpanel 冲突、包重复安装/升级/卸载，以及词库、学习数据和用户 YAML 保留。未完成完整发布验收前，不合并为正式 Ubuntu 默认路线。

## 已执行验证（2026-10-15）

测试机 `192.168.101.17`，Ubuntu 24.04.4、GNOME Shell 46.0 / Mutter 46.2、AMD GPU。源码位于 `/home/rich/bangke/ubuntu-gnome-panel/fcitx5-rime`，完整构建使用 `/home/rich/bangke/commit/bangke-librime`、`/home/rich/bangke/commit/bangke-ai-predict` 和 `/home/rich/bangke/vendor`；构建和输出与其他分支隔离。

- 完整源码编译、deb 组装及 `apt` 升级安装成功，测试版本 `5.0.19+gnome1`。
- Qt `configuration_test`、`platformtheme_test` 通过。
- Node 模型测试 7 项、安装生命周期回归、Ubuntu 包版本保护回归通过。
- GNOME 真实合成器中验证模糊开关、圆角裁剪、长文本、AI 更新原点保持和扩展禁用/恢复。模糊开启后的背景高频边缘能量由 7700 降至 952，避免仅以开关状态判断效果。
- 真实 Fcitx5 + GTK Wayland 按键：输入 `nihao` 显示 5 个候选，空格提交「你好」。测试等待首次词库部署完成，不用固定启动延时。
- 真实 Qt 设置保存测试验证横排/堆叠均由 GNOME 面板确认，Classic UI 配置未被改写。
- 扩展禁用后 Fcitx5 的 CurrentUI 返回 `classicui`，重新启用返回 `kimpanel`。

自动验收使用独立 HOME、XDG 目录、D-Bus 和虚拟显示器，不注销当前用户。可在装好测试包的机器上复现：

```bash
BANGKE_TEST_INSTALLED_INPUT=1 bash panel-gnome/test-shell.sh /tmp/bangke-gnome-results
```

仅验证 Shell 绘制与协议时省略 `BANGKE_TEST_INSTALLED_INPUT=1`。输出包含原始截图、面板状态、合成器日志和 Fcitx 日志。当前登录桌面首次安装后仍需注销、重新登录以发现系统扩展。

### GNOME 配置完整性修复（5.0.19+gnome2）

已安装扩展但当前会话尚未发现它时，设置页仍显示完整面板配置，圆角范围为 0–30 pt；保存外观到 YAML 和 appearance.json，并明确提示注销登录后加载。已有第三方面板时保留原行为。GNOME 新增逐字竖排、独立高亮圆角及透明底色排除；后者仅移除选中区域的背景填色，保留毛玻璃和文字。

验证包括真实 Qt 保存所有数值控件、外观开关和候选格式，检查 D-Bus 接收值及重新打开设置后的数值；真实 GNOME 截图比较圆角 0/30 与透明叠加开关，并验证扩展重新启用后读取持久配置。

### 5.0.19+gnome3 行为修复

- 程序自动探测运行中的候选面板，并在打开设置或保存时尝试启用已被桌面发现的适配器；不要求用户选择渲染后端，也不抢占第三方面板。
- 同一次拼写期间锁定首个候选列表的面板高度，AI 更新和候选减少均不缩放面板，超出部分在内部滚动；上屏或取消后重置，修改外观时重新测量。
- 高亮铺满候选单元格并统一由面板轮廓裁切：首项上圆角，中间直角，末项下圆角；列表填满锁定后的视口，避免底部留空破坏末项轮廓。
- 保存信息放在设置页状态栏；真正的警告对话框显式使用设置页的背景色、文字色。配置保存不要求注销，不可用时保留配置并提示效果暂不可用。
- GNOME Shell 会缓存已加载的扩展 JavaScript。升级本扩展代码与修改外观配置是两种操作：已有会话加载旧代码时，新渲染代码在下一次桌面登录加载；正常外观保存通过现有 D-Bus 接口即时生效。首次安装尚未发现扩展时自动保留系统候选面板，不能宣称所有效果已经应用。

验证：模型生命周期、真实 GNOME 长短候选高度相等、首中末高亮截图、圆角及透明叠加像素差异、实际 GTK 中文输入、真实 Qt 完整外观保存/重新读取、浅深色保存提示对话框截图，以及无活动适配器时的完整编辑器和持久保存。

### 5.0.19+gnome4 托盘与候选间距修复

Ubuntu 与 Omarchy 共用 `panel-omarchy/tray` 的 Fcitx NotificationItem 引用模块，让 Kimpanel 模式仍保留标准托盘和输入法设置菜单。Ubuntu 24.04 的公共模块头需要预先包含 HandlerTable 声明，已补齐，不修改系统头文件。

固定尺寸带来的剩余空间只由最后一个单元格吸收：横排候选保持自然宽度，竖排前几项保持自然高度，最后一项文字仍靠上；面板外轮廓和首中末高亮规则保持一致。回归检查在已扩宽的 AI 面板中切换成短候选，直接断言前两个单元格及文字起点的距离；真实 Fcitx/Kimpanel 验证托盘 Active、蚌壳设置菜单存在及菜单点击启动真实配置程序。

托盘兼容模块延迟到输入法启动后恢复注册，并在已注册托盘被 UI 切换撤销时通过事件回调恢复，避免 Classic UI 的布尔 enable/disable 接口覆盖初次启用。打包目录取自 Fcitx 的 pkg-config libdir，适配 Ubuntu multiarch 和 Arch。安装生命周期从用户会话导入 XAUTHORITY，避免托盘启动 Qt 设置时因缺少 XWayland 认证而退出。已在 `.17` 的真实托盘菜单点击「蚌壳拼音 设置」，确认配置窗口进程正常运行。
