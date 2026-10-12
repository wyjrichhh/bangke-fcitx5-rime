# fcitx5-rime Linux 构建指南（含 librime-ai-predict 插件）

本文档记录 Linux 构建/打包/发布/验收的完整路径，供后续迭代复用。
最后更新：2026-10-09，产物 `蚌壳拼音-5.0.12-linux-x64.tar`（254M，内含自包含 deb，通吃 22.04/24.04）。

## 一、架构决策（改代码前必读）

| 组件 | 版本/来源 | 原因 |
|---|---|---|
| fcitx5 | 发行版自带（22.04=5.0.14 / 24.04=5.1.7） | 用户机器本来就有，**不升级** |
| fcitx5-rime | 基线 **5.0.11** + reserved-property 移植 | master 要求 fcitx5 5.1.22（未发布），无法分发 |
| librime | **1.16.1** 自带 | 插件需要 `RefreshNonConfirmedComposition`（≥1.10.0），22.04 的 1.7.3 不够 |
| librime-ai-predict 插件 | 自带，静态链 CTranslate2 | 插件本体（`main` 分支，独立仓库 `wyjrichhh/librime-ai-predict`） |
| 依赖库（boost/icu/yaml-cpp/glog/…） | **捆绑共享库** | 跨发行版 SONAME 会变，打进包实现自包含 |

核心策略：**用最老的发行版（22.04）做构建基线**，glibc 向后兼容（老版本编 → 新版本能跑，反过来不行）。

## 二、构建环境

- 构建机：`192.168.101.17`（Ubuntu 24.04，已装 docker.io + 国内镜像加速 `/etc/docker/daemon.json`）
- 构建容器：**`ubuntu:22.04`**（必须，别换 24.04）
- 登录用户 `rich`（已配免密 sudo、在 docker 组）

### 开源库 vs 私有库（2026-10 起明确区分）

| | 本机 | 构建机 | 说明 |
|---|---|---|---|
| **私有库**（品牌化，**改动只进这里**） | `~/dev/bangke/bangke-fcitx5-rime` | `~/bangke/commit/fcitx5-rime` | 构建机上那份是 git 克隆（分支 `feat/linux-integration`，origin 写着 `wyjrichhh/fcitx5-rime` —— 旧仓库名，GitHub 会重定向到 `bangke-fcitx5-rime`）。**机器上没有 GitHub 凭据，`git pull` 拉不动**，所以实际靠 `dist/sync-to-box.sh` 从 Mac tar-over-ssh 覆盖工作树（保留那边的 `.git`） |
| librime（私有 fork） | `~/dev/open-source/librime` | `~/bangke/commit/librime` | 分支 `feat/linux-22.04-compat`，origin `wyjrichhh/librime` |
| 插件（私有 fork） | `~/dev/open-source/librime-ai-predict` | `~/bangke/commit/librime-ai-predict` | 分支 `feat/linux-integration`，origin `wyjrichhh/librime-ai-predict` |
| **开源库**（上游，**已定型不许写**） | `~/dev/open-source/fcitx5-rime` | `~/bangke/openSource/fcitx5-rime` | 上游 fcitx5-rime |

私有库的内容：`src/`（移植版 addon）、`config-ui/`（Qt5 设置程序）、`data/bangke.yaml`（配色真源）、`dist/`（构建/打包/同步脚本）、`LINUX_BUILD.md`。

### 构建机 `~/bangke/` 目录对应（2026-10-09 实测）

| 路径 | 是什么 |
|---|---|
| `commit/fcitx5-rime/` | **私有库工作树**；产物在它下面的 `build-out/` |
| `commit/librime/`、`commit/librime-ai-predict/` | 两个私有 fork 的克隆 |
| `openSource/` | **第三方产物区**（构建时挂成容器里的 `/src`，`/priv` 则是私有库）。里面 `librime/`（1.16.1 源码 + 构建产物）、`bundle-libs/`（捆绑共享库）、`deb-resources/`（模型 + 默认配置 + 雾凇 + 字体）、`im-brand.env`（品牌名兜底，优先取 `/priv/dist/im-brand.env`）—— **这几棵是纯目录、不是 git 克隆** |
| `models/` | 模型原始物料（`lgb_v4`、`lgb_v5`、`t19_ep1`、`t19_ep4`、`tarballs`） |
| `uitest/` | panel-harness 截图的落点 |
| `bk-reinstall.sh` | 机器重装脚本 |

`openSource/` 里另外堆着一批**历史遗留，都不是事实来源、别在里面改**：早期私有源码（`fcitx5-rime-5.0.11/`、`fcitx5-rime/`、`fcitx5/`、`config-ui/`、`deb-stage/`、`stage-final/`），以及散落进去的脚本和旧包（`repack*.sh`、`sync-to-box.sh`、`panel-harness.sh`、`fcitx5-rime-ai-predict_5.0.1{1,2}_amd64.deb`）。

打包产物与暂存目录在**私有侧**：`~/bangke/commit/fcitx5-rime/build-out/`（`stage-final/` + `*.deb` + tar）。

## 三、同步代码（Mac → 远程）

远程直连 GitHub 不稳（国内网络），改为 Mac clone 后 scp（tar-over-ssh）：
- librime / 插件 / CTranslate2 源码：从 Mac 的 `~/dev/open-source/librime` scp，排除 `.git` / `build` / `deps` / `lib` / `include`（Mac 构建产物和 CT2 的 Mac 版）
- 注意：scp 会带 macOS AppleDouble 文件（`._*`），落地后 `find . -name '._*' -delete`

## 四、构建脚本（都在私有库 `~/bangke/commit/fcitx5-rime/dist/`）

1. **`build-22.04.sh`** —— 容器内 5 步：装依赖 → 构建 CTranslate2 → librime+插件 → fcitx5-rime → **config-ui（Qt5）**。`SKIP_LIBRIME=1` 复用 `/src/librime/build` 里已有的 librime 产物（只改了私有代码时用，省十几分钟）
2. **`repack-container.sh`** —— 容器内：staging 进 `build-out/stage-final/` → 注入模型/配置/字体/主题 → 写 DEBIAN → `dpkg-deb` → 打 tar → 生成「双击安装」脚本

（`pkg-22.04.sh`、`pkg-host.sh`、`build-deb.sh`、`rebuild-ct2.sh`、`boost-*.sh`、`test-24.04.sh`、`test-final.sh` 等是早期路线的遗留，脚本之间已无人调用。）

一条命令构建：
```bash
# 构建（私有源码来自 /priv，第三方产物来自 /src）
docker run --rm -e SKIP_LIBRIME=1 \
  -v ~/bangke/commit/fcitx5-rime:/priv -v ~/bangke/openSource:/src \
  ubuntu:22.04 bash /priv/dist/build-22.04.sh
# 打包（只做 staging + dpkg-deb，不编译）
docker run --rm \
  -v ~/bangke/commit/fcitx5-rime:/priv -v ~/bangke/openSource:/src \
  ubuntu:22.04 bash /priv/dist/repack-container.sh
```

## 五、librime 的 22.04 兼容补丁（重编时必须保留）

librime 1.16.1 是 bleeding-edge，在 Ubuntu 22.04（boost 1.74 / glog 0.5 / cmake 3.22）上需要：

1. **`CMakeLists.txt`**：
   - `if(LINUX)` → `if(CMAKE_SYSTEM_NAME STREQUAL "Linux")`（**关键 bug**：`LINUX` 变量从未定义，导致走了 `else` 分支 `find_package(Boost 1.77)` 在 boost 1.74 上失败 → boost 完全没链接）
   - 加 `set(Boost_NO_BOOST_CMAKE ON)`（强制 Module 模式，否则 `${Boost_LIBRARIES}` 为空）
   - `find_package(Boost 1.74.0 REQUIRED COMPONENTS regex filesystem system)`（补 filesystem/system 组件）
2. **`plugins/plugins_module.cc`**：`boost::dll::shared_library(plugin_file)` → `(plugin_file.string())`（boost 1.74 不接 `std::filesystem::path`）
3. **`src/rime/setup.cc`**：注释 `google::SetLogSymlink(...)`（glog 0.6 移除；0.5 有但无害）
4. **`src/rime/setup.cc` + 插件 `predict_module.cc`**：去掉 `google::IsGoogleLoggingInitialized()`（glog 0.5 没有；setup 用无条件 `InitGoogleLogging`，插件 finalize 用无条件 `ShutdownGoogleLogging`，`EnsurePluginLoggingInitialized` 用 static bool 替代）
5. **删除 `include/` 里的 glog/yaml-cpp/leveldb/marisa/opencc/gtest 头文件**（Mac `make deps` 残留的旧版头，会导致与系统库 API 不匹配；只保留 librime 自己的 darts/utf8/X11）

## 六、fcitx5-rime 5.0.11 移植（reserved-property 协议）

改动文件：`src/reservedproperty.h`（新增）、`rimeengine.{h,cpp}`、`rimestate.{h,cpp}`、`rimecandidate.{h,cpp}`。

与 master 版移植的差异（5.0.11 是 `namespace fcitx`，master 是 `fcitx::rime`）：
- 5.0.11 **没有** `notifyImmediately`/`currentKeyEventState` → 丢掉同步缓存优化，只保留 `notify()` → `handleReservedProperty` 路径
- 5.0.11 **没有** `foreachFocused` → 用 `instance_->inputContextManager().foreach(...)` 替代
- 5.0.11 的候选 comment 是**内联拼进 text**（`text.append(comment, flag)`），不是独立 comment 字段
- addon 输出是 `rime-ai.so`（`PREFIX ""`，addon 名见 `src/CMakeLists.txt`），不是 master 的 `librime.so`
- 5.0.11 需要给 `RimeState` 加 `session()` getter（原只有私有 `session_`）
- `po/CMakeLists.txt` 禁用 `fcitx5_install_translation`（gettext 0.21 的 `.mo.rule` bug）

reserved-property 协议本身（`_refresh_ui` / `_comment_highlight` / `_comment_warning`）见 `src/reservedproperty.h` 注释，与 squirrel 的 `ReservedProperty.swift`、插件的 `frontend_protocol.h` 对应。

## 七、打包产物

`build-out/` 里三个文件：

| 文件 | 用途 |
|---|---|
| **`蚌壳拼音-<版本>-linux-x64.tar`**（254M） | **给用户的**：解开是下面两个，且**可执行位保留** |
| `fcitx5-rime-ai-predict_<版本>_amd64.deb` | 安装包本体 |
| `蚌壳拼音-安装.sh` | 双击安装脚本（品牌名已注入） |

版本号在 `repack-container.sh` 顶部的 `VERSION` —— 它与源码基线**无关**：`CMakeLists.txt` 里 `project(fcitx5-rime VERSION 5.0.11)` 指上游基线，包版本是我们自己往上加的（现在 5.0.12）。**tar 名必须带版本号** —— 官网清单只从文件名读版本（见下一节）。

deb 装到哪儿（159 个文件）：

- `/usr/lib/x86_64-linux-gnu/fcitx5/rime-ai.so` —— addon 本体。addon 名是 `rime-ai`（不是上游的
  `rime`）：名字同时写死在两个 conf 的文件名、`Library=` 和 profile 的 `Name=` 里，四处必须一致，
  改名时一起动。已装用户由 postinst / `.INSTALL` 的 `migrate_from_rime` 迁移（profile 条目与
  `conf/rime.conf`）。
- `/usr/lib/fcitx5-rime-ai/` —— 捆绑的 17 个 `.so`（librime、ctranslate2、boost_regex、icu×3、yaml-cpp、glog、leveldb、marisa、opencc、snappy、gflags）+ `rime-plugins/librime-ai-predict.so`；`/etc/ld.so.conf.d/fcitx5-rime-ai.conf` 指过去
- `/usr/bin/fcitx5-rime-config` —— Qt5 设置程序
- `/usr/share/fcitx5-rime/predict_models/zh-base-ct2-int8/model.bin`（236M）
- `/usr/local/share/fcitx5/addon/rime-ai.conf`、`/usr/share/fcitx5/inputmethod/rime-ai.conf` —— addon
  描述与输入法条目。两条 install 规则的目标目录不同：addon 那条走 `FCITX_INSTALL_PKGDATADIR`
  （容器里解析成 `/usr/local/share`，Ubuntu 的 fcitx5 会扫 XDG 的 `/usr/local/share`，所以能用；
  **Arch 的 fcitx5 只扫 `/usr/share`，那边脚本会显式挪过去**）。
- postinst 给用户写 `~/.config/fcitx5/profile`（`DefaultIM=rime-ai`）、`classicui.conf`、`~/.local/share/fcitx5/rime/bangke.yaml`、主题 `themes/BangKe/`

`Depends: fcitx5, libc6, libstdc++6, libgcc-s1, libqt5widgets5, libqt5dbus5` —— 全新容器里没有 fcitx5 也能装上（安装脚本用 `apt-get -f install` 自动补，实测 22.04/24.04 都过）。

## 八、发布（上架官网）

发的是 **tar 而不是裸 deb**：归档才保得住安装脚本的可执行位，从浏览器直接下 `.sh` 会丢 `+x`、双击只会用文本编辑器打开。

1. 出包：`repack-container.sh` → `build-out/蚌壳拼音-<版本>-linux-x64.tar`
2. 把 tar 拷进 `~/dev/bangke/bangke-dist/`（`sync-downloads.sh` 的 Linux 来源就是这儿，和 Windows 同一个约定）
3. 上架：

```bash
cd ~/dev/bangke/bangke-site
SSHPASS="$(cat ~/.bangke-sshpass)" ./release.sh ~/dev/bangke/bangke-dist/蚌壳拼音-<版本>-linux-x64.tar
```

- 密码在 `~/.bangke-sshpass`（600）；服务器 `root@182.151.24.30:/srv/bangke-site/downloads`
- `release.sh` 六步：校验 → 放包 → 重算清单（**新包必须被 `gen-releases.mjs` 的正则收录，否则官网卡片会静默显示「即将推出」**）→ 归档旧包 → 上传 → 回读 sha256。上传成功之前任何一步失败都会回滚本地改动、服务器不受影响
- 官网源码只在**这台 Mac**（`~/dev/bangke/bangke-site`），而且**不在任何 git 仓库里** —— 改动只躺在这台机器的磁盘上，没有版本控制兜底。构建机 17 上没有站点源码，服务器上只有镜像 + `downloads/`

清单 `downloads/releases.json` 是唯一事实来源：前端用它覆盖 `content.ts` 的兜底值，**换包不用重编前端**，卡片最多 5 分钟刷新。已知坑：本机代理会让 `curl` 服务器拿到**假 502**（要 `--noproxy '*'`）；线上域名仓库里没记录，HTTP 层验不了，只能靠 `release.sh` 第 6 步的文件系统回读。

## 九、验收

- ✅ 干净容器装包（22.04 / 24.04，amd64）：依赖全解析（`not found=0`）、输入法注册 `DefaultIM=rime`、主题与 seed 生成、模型就位、设置程序 offscreen 能起
- ✅ GUI 端到端：`dist/panel-harness.sh` 在容器里跑真 fcitx5 截图（**GTK 前端模块名要按实际存在的选**：22.04 只有 `im-fcitx5.so`，24.04 两个都有；写死 `GTK_IM_MODULE=fcitx` 会让打字根本不进 fcitx5，表现为「没有候选、字直接进输入框」）
- 本机（Mac/arm64）验 amd64 包：`docker run --platform linux/amd64`（走 Rosetta；解 266MB 很慢，别设短 timeout）。两个坑：`ubuntu` 镜像自带 `docker-clean`，apt 跑完就删 `/var/cache/apt/archives/*.deb`；本机代理会随机给 apt **假 502**，`-o Acquire::Retries=8` 重跑即可，**别误判成依赖缺失**

## 十、后续修改的复用路径

| 改什么 | 改哪里 | 重跑什么 |
|---|---|---|
| fcitx5-rime / config-ui 代码 | 私有库 `~/bangke/commit/fcitx5-rime/`（本机 `bangke-fcitx5-rime`） | `dist/sync-to-box.sh` → `build-22.04.sh`（`SKIP_LIBRIME=1`）→ `repack-container.sh` |
| 插件 / librime | 构建读的是 `~/bangke/openSource/librime/`（构建树）；对应的 git 克隆是 `~/bangke/commit/librime` | `build-22.04.sh`（不带 `SKIP_LIBRIME`，很慢） |
| 模型 / 配置 / 字体 | 构建机 `~/bangke/openSource/deb-resources/` | `repack-container.sh` |
| 仅改打包逻辑 | 私有库 `dist/repack-container.sh` | `repack-container.sh` |
| 上架官网 | `~/dev/bangke/bangke-site`（`gen-releases.mjs` 的正则、`content.ts` 文案） | 见第八节 `release.sh` |

始终用 `ubuntu:22.04` 容器构建（**包括 config-ui**：24.04 容器编出来的二进制链的是 glibc 2.39，在 22.04 上跑不起来）。若要覆盖更老发行版（如 20.04），继续用更老的基线。

产物落在私有侧：`~/bangke/commit/fcitx5-rime/build-out/`。
