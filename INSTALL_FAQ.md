# Linux 安装与排查

## Omarchy / Hyprland

选择对应架构的 `.pkg.tar.zst`，使用 `sudo pacman -U ./包文件` 安装或升级。包替代发行版 `fcitx5-rime`，不能与它同时安装；首次安装后注销并登录。卸载使用 `sudo pacman -R bangke-pinyin`。

设置通过系统托盘打开，也可执行 `fcitx5-rime-config`。跟随系统主题需在外观页选择「跟随 Omarchy 系统主题」，深色方案选「跟随浅色方案」或系统主题；固定选择某方案会保留该选择。

候选面板、光标和托盘异常时查看：

```bash
systemctl --user status bangke-panel bangke-caret
journalctl --user -u bangke-panel -u bangke-caret -n 100 --no-pager
cat ~/.cache/bangke-pinyin/setup.log
cat ~/.cache/bangke-pinyin/settings.log
```

`XDG_CACHE_HOME` 自定义时日志跟随该路径。常规面板日志不逐键输出位置；需要详细诊断时在用户服务中设置 `BANGKE_PANEL_DEBUG=1`，复现后移除该环境配置。

Hyprland 更新后旧插件 ABI 可能不匹配，需要重新登录；不要强制加载旧 `.so`。插件源码在本机按匹配头文件编译，关闭毛玻璃不应影响透明度。输入卡顿复现时记录服务日志与 CPU 占用，勿反复覆盖用户配置。

## Ubuntu 24.04 amd64

使用 `sudo apt install ./fcitx5-rime-ai-predict_<版本>_amd64.deb`，自动补齐依赖。首次安装后注销并登录。GNOME Shell 46 包含实验候选面板扩展，首次安装需要注销、登录后加载；已有其他 Kimpanel 时保留原面板。扩展未启用时使用 Classic UI。当前实现已通过独立 GNOME 合成器和 GTK 中文输入测试，多屏及混合缩放等发布验收待完成，详见 `panel-gnome/README.md`。

## 架构或动态库不匹配

通过 `uname -m` 区分 `aarch64` 和 `x86_64`，不要混用 Arch 与 Ubuntu 包。缺少共享库时检查包依赖和发行版版本；不要从其他发行版复制 `.so`。Arch 为滚动发行版，发布包需要在受支持的系统环境验收。

## 升级与用户数据

升级同步包管理的种子数据，同时保留用户自定义配置与学习词库。不要删除整个用户 Rime 目录处理故障。清理旧实验启动项的迁移程序只匹配已知路径，并会保留备份；无需再运行旧共享目录中的安装或补丁脚本。
