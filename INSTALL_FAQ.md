# fcitx5-rime-ai-predict 安装常见问题（FAQ）

面向分发安装包的最终用户，收录安装/使用失败的常见问题和解决办法。
（本文针对 `fcitx5-rime-ai-predict_5.0.11_amd64.deb`）

## 0. 安装前确认

本包要求：
- Ubuntu 22.04 或 24.04（更老发行版见问题 2，其它发行版见问题 2/3）
- x86_64（amd64）架构
- 已安装 fcitx5（本包是 fcitx5 的输入法插件，不自带 fcitx5 框架）

## 1. 安装时报「依赖不满足」/ dependency problems

**症状**：`sudo dpkg -i fcitx5-rime-ai-predict_5.0.11_amd64.deb` 报错：
```
dpkg: dependency problems prevent configuration of fcitx5-rime-ai-predict:
 fcitx5-rime-ai-predict depends on fcitx5; however: Package fcitx5 is not installed.
```

**原因**：本包依赖 fcitx5 框架，机器上还没装。

**解决**：
```bash
sudo apt-get update
sudo apt-get install -f      # 自动补齐缺失依赖
# 或显式先装 fcitx5
sudo apt-get install fcitx5
sudo dpkg -i fcitx5-rime-ai-predict_5.0.11_amd64.deb
```

## 2. 运行时报 `version 'GLIBC_x.xx' not found`

**症状**：安装成功，但启动时提示 `version 'GLIBC_2.35' not found` 之类。

**原因**：系统 glibc 太老。本包在 Ubuntu 22.04 构建，需要 glibc ≥ 2.35。Ubuntu 20.04（glibc 2.31）、Debian 11 等更老的发行版跑不了。

**解决**：升级到 Ubuntu 22.04 或更新版本；无法升级则联系分发方要对应旧发行版的构建。

## 3. 报 `package architecture does not match`

**症状**：
```
package architecture (amd64) does not match system (arm64)
```

**原因**：本包是 x86_64 架构，机器是 ARM（树莓派、Apple Silicon 虚拟机等）。

**解决**：需要 arm64 版本，联系分发方获取对应架构的包。

## 4. 装完 fcitx5 里没有「中州韵 / Rime」输入法

**症状**：安装成功，但 fcitx5 输入法列表里没有 rime。

**解决**：
1. 重启 fcitx5：
   ```bash
   fcitx5 -r       # 或注销后重新登录
   ```
2. 用 fcitx5 配置工具 → 附加组件 → 确认「Rime」已勾选启用。
3. 确认 addon 文件在位：`ls /usr/local/lib/fcitx5/rime.so`

## 5. 切到 rime 但打不出字 / 无候选

**排查**：
1. 确认依赖都解析（不应有 `not found`）：
   ```bash
   sudo ldconfig
   ldd /usr/local/lib/fcitx5/rime.so | grep "not found"
   ```
2. 确认 rime 共享数据在位：`ls /usr/share/rime-data/`（应有 `default.yaml`、`fcitx5.yaml`）。
3. 看 fcitx5 / rime 日志定位具体报错。

## 6. 普通候选正常，但 AI 预测候选不出现

**排查**（按顺序）：
1. 插件在位：`ls /usr/local/lib/rime-plugins/librime-ai-predict.so`
2. 模型在位：`ls -lh /usr/local/share/fcitx5-rime/predict_models/zh-base-ct2-int8/model.bin`（约 237M）
3. 配置已启用 AI 模块：`~/.local/share/fcitx5/rime/default.custom.yaml` 里有 `'modules/@next': ai_predict`
4. 配置里 `ai_predict/model_path` 指向模型目录：
   `grep -r model_path ~/.local/share/fcitx5/rime/*.custom.yaml`
5. 看插件日志确认推理是否在跑（glog 输出，通常在 fcitx5 启动日志或 `$TMPDIR` 下 `rime.*.ai_predict.*` 文件）。

## 7. 装完没有默认配置 / 配置没生效

**症状**：装完没有 luna_pinyin 等 schema，或 AI 功能没启用。

**原因**：安装脚本（postinst）会把默认配置 seed 到「新用户模板 `/etc/skel`」和「当前 sudo 用户」。若以其它方式安装（非 sudo、或直接 root 装），已有用户可能没拿到配置。

**解决**：
```bash
mkdir -p ~/.local/share/fcitx5/rime
cp -a /usr/local/share/fcitx5-rime/seed/. ~/.local/share/fcitx5/rime/
```
然后 `fcitx5 -r` 重启。

## 8. 卸载

```bash
sudo dpkg -r fcitx5-rime-ai-predict
```
卸载**不会**删除用户自己的配置（`~/.local/share/fcitx5/rime/` 会保留）。

## 9. 通用排查命令合集

```bash
# 是否已装
dpkg -l | grep fcitx5-rime

# 依赖是否解析（有 not found 就是有问题）
sudo ldconfig
ldd /usr/local/lib/fcitx5/rime.so                    | grep "not found"
ldd /usr/local/lib/librime.so.1.16.1                 | grep "not found"
ldd /usr/local/lib/rime-plugins/librime-ai-predict.so | grep "not found"

# 关键文件是否在位
ls /usr/local/lib/fcitx5/rime.so \
   /usr/local/lib/rime-plugins/librime-ai-predict.so
ls -lh /usr/local/share/fcitx5-rime/predict_models/zh-base-ct2-int8/model.bin

# 重启 fcitx5
fcitx5 -r
```
