# ESP32 平台手动安装指南（PlatformIO + ESP-IDF）

> 适用于在装有 PlatformIO 的机器上安装 `espressif32` 平台及 ESP-IDF 框架。
> 本文以 Windows + Git Bash 为例，Linux / macOS 命令基本一致（路径换成 `~/.platformio`）。

---

## 0. 确认 PlatformIO 可用

```bash
export PATH="$HOME/.platformio/penv/Scripts:$PATH"   # Windows Git Bash 路径
pio --version                                        # 应输出 PlatformIO Core, version 6.x
```

> Windows 用户也可直接使用 `pio`（安装时已加入 PATH），无需 export。

---

## 1. 方式一：命令行手动安装平台（推荐）

PlatformIO 全局安装 `espressif32` 平台（含 ESP-IDF 框架与编译工具链，体积约 1~2 GB）：

```bash
# 安装最新版 espressif32 平台
pio pkg install --platform espressif32

# 或安装指定版本（例：5.x 老版本）
pio pkg install --platform "espressif32@5.3.0"
```

安装完成后验证：

```bash
pio pkg list --global
```

输出中应看到：

```
Platforms
├── espressif32 @ 7.0.1 (required: platformio/espressif32)
Tools
├── framework-espidf @ 4.6xxxxx
├── toolchain-xtensa-esp-elf @ ...   # ESP32 / S2 / S3 编译工具链
├── toolchain-riscv32-esp    @ ...   # ESP32-C 系列工具链
├── tool-esptool            @ ...    # 烧录工具
├── tool-cmake / tool-idf / tool-mconf ...
```

---

## 2. 方式二：让 pio run 自动安装

在工程目录（含 `platformio.ini`）直接执行：

```bash
cd ESP32
pio run
```

首次运行会**自动联网**下载并安装 `platformio.ini` 中声明的平台与框架。
如果自动安装中途被中断，重跑 `pio run` 会从断点继续。

---

## 3. 方式三：离线 / 内网安装

若目标机器无法直连 PlatformIO 服务器（registry 下载失败），可在一台能联网的机器上先装好，再整体拷贝：

```bash
# 源机器
pio pkg install --platform espressif32
# 将整个目录拷贝到目标机器（注意版本对应）
# Windows:  C:\Users\<user>\.platformio
# Linux:    ~/.platformio
```

拷贝后需保持 `~/.platformio/platforms`、`~/.platformio/packages`、`~/.platformio/.cache` 三者一致。

---

## 4. 常见问题排查

| 问题 | 处理 |
|---|---|
| 下载慢 / 超时 | 重试 `pio pkg install`；或设置代理后重试 |
| 平台装好但 `pio run` 仍提示找不到平台 | 检查 `platformio.ini` 的 `platform = espressif32` 拼写；`pio pkg list --global` 确认已装 |
| 提示 `toolchain-xtensa-esp-elf` 缺失 | 平台安装时未下载完整工具链，执行 `pio pkg install -f` 修复 |
| 防火墙 / 内网 | 用方式三整体拷贝 `.platformio` 目录 |
| `pio run` 卡在 Framework (espidf) 阶段 | 首次需下载 ESP-IDF，耐心等待；可设置环境变量 `PLATFORMIO_IGNORE_GIT_SUBMODULES=true` 加速 |

---

## 5. 本项目验证结果（2026-08-22）

本项目机器上安装状态（`pio pkg list --global`）：

| 组件 | 版本 |
|---|---|
| espressif32 平台 | 7.0.1 |
| framework-espidf | 4.60001.0 |
| toolchain-xtensa-esp-elf | 15.2.0+20251204 |
| toolchain-riscv32-esp | 15.2.0+20251204 |
| tool-esptool | 1.413.0 |

环境已就绪，可直接编译：

```bash
cd ESP32
pio run
```

---

## 6. 构建踩坑与修复记录（espressif32 7.0.1 + ESP-IDF 6.0.1）

### 6.1 主目录是 git 仓库导致 CMake 崩溃

**症状**：`pio run` 报 `CMake Error ... grabRef.cmake:48: file failed to open ... head-ref`。

**原因**：用户主目录（如 `C:\Users\<user>`）本身是一个 git 仓库，`git rev-parse --git-dir` 在 `framework-espidf` 目录内向上搜索时错误命中主目录的 `.git`，而该仓库 refs 不完整，导致 ESP-IDF 的版本检测 fatal。

**修复**：构建前注入 `GIT_CEILING_DIRECTORIES`，限制 git 向上搜索的边界：

```bash
# Windows Git Bash
export GIT_CEILING_DIRECTORIES="$HOME/.platformio"
pio run
```

> 该环境变量是 git 官方机制：git 不会越过该目录向上查找仓库。可写入 shell 配置（`~/.bashrc` / `~/.profile`）持久生效。

### 6.2 esptool 缺少 intelhex 模块

**症状**：`pio run` 在生成 `bootloader.bin` 时崩溃：
`ModuleNotFoundError: No module named 'intelhex'`。

**原因**：`tool-esptoolpy` 包自带的 esptool.py 需要 `intelhex`，但 PlatformIO 的 venv 未包含。

**修复**：给 PlatformIO 的 Python 环境补装：

```bash
# Windows Git Bash
"$HOME/.platformio/penv/Scripts/python.exe" -m pip install intelhex
```

### 6.3 Flash 容量告警

**症状**：`Warning! Flash memory size mismatch detected. Expected 8MB, found 2MB!`

**原因**：`esp32-s3-devkitc-1` 板型默认 8MB Flash；旧配置与实际模块容量不一致。

**修复**：按实际模块覆盖。本仓库实际使用 **ESP32-S3-WROOM-1-N16R8（16 MB Flash + 8 MB Octal PSRAM）**，在 `platformio.ini` 中：

```ini
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.partitions = partitions_16mb_4m.csv
```

> 若你手上的模块不是 16 MB，请按实际容量修改 `platformio.ini` 与 `sdkconfig.esp32-s3` 中的 `CONFIG_ESPTOOLPY_FLASHSIZE`，并选用合适的 `partitions_*.csv`。
