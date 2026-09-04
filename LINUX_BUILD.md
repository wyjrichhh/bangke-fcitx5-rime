# fcitx5-rime Linux 构建指南（含 librime-ai-predict 插件）

本文档记录 Linux 构建/打包的完整路径，供后续修改与编译复用。
最后更新：2026-09-01，产物 `fcitx5-rime-ai-predict_5.0.11_amd64.deb`（229M，自包含，通吃 22.04/24.04）。

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
- 目录：`~/bangke/openSource/`
  - `librime/`（librime 1.16.1 + 插件，CTranslate2 源码在 `plugins/librime-ai-predict/deps/`）
  - `fcitx5-rime-5.0.11/`（移植版源码）
  - `deb-resources/`（模型 + 默认配置 + 雾凇拼音）
  - `stage22/`（DESTDIR 打包暂存）
  - `bundle-libs/`（捆绑的共享库）

## 三、同步代码（Mac → 远程）

远程直连 GitHub 不稳（国内网络），改为 Mac clone 后 scp（tar-over-ssh）：
- librime / 插件 / CTranslate2 源码：从 Mac 的 `~/dev/open-source/librime` scp，排除 `.git` / `build` / `deps` / `lib` / `include`（Mac 构建产物和 CT2 的 Mac 版）
- 注意：scp 会带 macOS AppleDouble 文件（`._*`），落地后 `find . -name '._*' -delete`

## 四、构建脚本（都在 `~/bangke/openSource/`）

1. **`build-22.04.sh`** —— 容器内：装依赖 → 构建 CTranslate2 → librime+插件 → fcitx5-rime
2. **`pkg-22.04.sh`** —— 容器内：把产物 `DESTDIR` 进 `stage22/`
3. **`pkg-host.sh`** —— 宿主机：注入模型/配置 + 写 DEBIAN + `dpkg-deb`

一条命令构建：
```bash
docker run --rm -v ~/bangke/openSource:/src ubuntu:22.04 bash /src/build-22.04.sh
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
- 5.0.11 addon 输出是 `rime.so`（`PREFIX ""`），不是 master 的 `librime.so`
- 5.0.11 需要给 `RimeState` 加 `session()` getter（原只有私有 `session_`）
- `po/CMakeLists.txt` 禁用 `fcitx5_install_translation`（gettext 0.21 的 `.mo.rule` bug）

reserved-property 协议本身（`_refresh_ui` / `_comment_highlight` / `_comment_warning`）见 `src/reservedproperty.h` 注释，与 squirrel 的 `ReservedProperty.swift`、插件的 `frontend_protocol.h` 对应。

## 七、打包产物

- `fcitx5-rime-ai-predict_5.0.11_amd64.deb`（229M）
- Depends 仅 `fcitx5, libc6, libstdc++6, libgcc-s1`
- 捆绑 11 个共享库到 `/usr/local/lib/`（boost_regex、icu×3、yaml-cpp、glog、leveldb、marisa、opencc、snappy、gflags）+ `ld.so.conf.d` 条目指向 `/usr/local/lib`
- 模型装到 `/usr/local/share/fcitx5-rime/predict_models/zh-base-ct2-int8/`，`model_path` 绝对路径注入 seed 配置

## 八、验证

- ✅ 干净 ubuntu:24.04 容器装包 + `ldd` 全解析（三个 .so 的 `not found` 都 = 0）
- ✅ addon 在 fcitx5 5.1.7 上 ABI 兼容（`libFcitx5Core.so.7` 解析成功）
- ⏳ 待办：GUI 端到端（启动 fcitx5 → 打字出 AI 候选）需有显示环境

## 九、后续修改的复用路径

| 改什么 | 改哪里 | 重跑什么 |
|---|---|---|
| fcitx5-rime 代码 | `fcitx5-rime-5.0.11/` | `build-22.04.sh`（或只重编该目录） |
| 插件 / librime | `librime/` | `build-22.04.sh` |
| 模型 / 配置 | `deb-resources/` | `pkg-22.04.sh` + `pkg-host.sh` |
| 仅改打包逻辑 | `pkg-host.sh` | `pkg-host.sh` |

始终用 `ubuntu:22.04` 容器构建。若要覆盖更老发行版（如 20.04），继续用更老的基线。
