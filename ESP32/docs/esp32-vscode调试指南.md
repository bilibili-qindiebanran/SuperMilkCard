# ESP32-S3 VSCode 调试指南（PlatformIO + ESP-IDF）

> 本项目使用 **ESP32-S3 板载 USB-Serial/JTAG** 调试，**无需外接任何调试器**（如 ESP-Prog、J-Link）。
> 只需用数据线连接开发板的 **原生 USB 口** 即可断点调试。

---

## 1. 前提条件

| 项目 | 说明 |
|---|---|
| VSCode | 已安装 |
| PlatformIO IDE 扩展 | 已安装（本项目机器 3.3.x） |
| ESP-IDF 平台 | 已安装（espressif32 7.0.1） |
| 数据线 | **支持数据传输**的 USB 线（不能是充电线） |
| 连接端口 | **原生 USB 口**（见下文） |

### ESP32-S3 DevKitC 的两个 USB 口

```
┌────────────────────────────────────────────┐
│                                            │
│   [BOOT] [EN]     USB-SERIAL/JTAG (USB)    │
│     ▲     ▲            ▲    ▲             │
│     │     │            │    └─ 调试/日志：连这个原生 USB 口   │
│     │     └─ EN=复位键（即 RST，按一下重启）    │
│     └─ BOOT=下载模式键（按住再按EN进下载模式）  │
│                                            │
│   ESP32-S3 DevKitC-1                       │
└────────────────────────────────────────────┘
```

> ⚠️ **ESP32-S3 没有标 "RST" 的按键，复位键就是 "EN"**（Enable）。
> - **EN（RST）**：按一下 = 复位重启，固件从 flash 正常运行。
> - **BOOT（IO0）**：按住再按 EN = 进入下载模式（此时无法看到日志）。
> 之前"看不到日志"正是因为芯片停在了下载模式。

- **调试（断点/单步）** → 连接 **原生 USB 口**（USB-Serial/JTAG，标 `USB`，靠 BOOT 键一侧）
- **仅串口监视** → 两个口都可以（本项目 console 已配置走 USB-Serial/JTAG）

---

## 2. 查看串口日志（不调试时）

不调试、只观察运行日志（如 IP5306 通讯结果）时，工程已配好"串口监视器"任务：

1. 打开 VSCode → 加载 `D:\Projects\SuperMilkCard\ESP32` 文件夹
2. 菜单 **终端 → 运行任务…**（Terminal → Run Task…）
3. 选择 **"串口监视器 (COM10 115200)"**
4. 日志会输出到新的终端面板；按 **`Ctrl+C`** 停止

> 若串口号变化（拔插 USB 后可能变），修改 `.vscode/tasks.json` 里该任务的 `-p` 参数，
> 或用 `pio device list` 查看当前串口。
>
> **看不到日志时**：先按一下板上 **EN 键** 复位（若芯片停在下载模式，需复位才会正常启动并输出日志）。

---

## 3. 一键调试（推荐）

工程在 `.vscode/launch.json` 中配好了两种调试方式：

| 调试配置 | 说明 |
|---|---|
| **PIO Debug**（F5 默认） | PlatformIO 扩展官方方案，自动编译→烧录→OpenOCD→GDB |
| **ESP32-S3 调试 (cppdbg + OpenOCD)** | 备份方案：VSCode 原生 cppdbg + 手动 OpenOCD，不依赖扩展动态命令 |

操作步骤：

1. 打开 VSCode 并加载工程：`File → Open Folder` 选择 `D:\Projects\SuperMilkCard\ESP32`
2. 连接开发板 **原生 USB 口** 到电脑
3. 在 `src/main.c` 中需要的位置打上断点（点行号左侧红点）
4. 按 **`F5`** 或点击左侧 **Run and Debug** → 选择上方任一调试配置

PlatformIO 方案（PIO Debug）会自动完成：**编译（带调试符号）→ 烧录 → 启动 OpenOCD 调试器**，随后停在 `app_main` 入口（`platformio.ini` 中 `debug_init_break = tbreak app_main`）。

### cppdbg 备份方案说明

若 PlatformIO 扩展调试链路异常（如报 `extension.launchBinary not found`），可改用 **"ESP32-S3 调试 (cppdbg + OpenOCD)"** 配置：
- 它使用 VSCode 原生 `cppdbg` 调试器 + 手动 OpenOCD 服务
- `debugServerArgs` 指定 `.vscode/esp32s3-usb-jtag.cfg`（板载 USB-JTAG 接口配置）
- `preLaunchTask` 先启动 OpenOCD 服务器，然后 GDB 连接 localhost:3333

### 常用调试操作

| 操作 | 快捷键 |
|---|---|
| 继续运行 | F5 |
| 单步跳过 | F10 |
| 单步进入 | F11 |
| 单步跳出 | Shift+F11 |
| 停止调试 | Shift+F5 |
| 查看变量 / 监视 | 左侧"运行和调试"面板 / Watch |

---

## 3. 关键配置说明

### `platformio.ini`

```ini
build_type = debug                 ; 编译带调试符号（-g -Og）
debug_tool = esp-builtin           ; 使用板载 USB Serial/JTAG 调试接口
debug_init_break = tbreak app_main ; 启动调试后停在 app_main 入口
```

- `debug_tool = esp-builtin` 对应 ESP32-S3 原生 USB-Serial/JTAG（该板型的 `default_tool`）。
- 如需停在别的函数（如 `ip5306_init`），改 `debug_init_break` 即可。
- 若想正常速度运行、只在断点处停，把 `build_type` 改回 `release`（调试符仍在，但优化级别更高）。

### `.vscode/launch.json`

使用 PlatformIO IDE 扩展的 `platformio-debug` 类型，`preLaunchTask` 会在调试前自动编译烧录。

---

## 4. 手动调试（命令行）

不依赖 VSCode 也可以调试：

```bash
cd ESP32
export GIT_CEILING_DIRECTORIES="$HOME/.platformio"

# 编译 + 烧录 + 启动调试（OpenOCD + GDB，交互式）
pio debug

# 指定工程环境
pio debug -e esp32-s3
```

> `pio debug` 是交互式 GDB，`continue` 运行、`break app_main` 下断点、`Ctrl+C` 中断、`quit` 退出。

---

## 5. 常见问题排查

| 现象 | 原因 / 解决 |
|---|---|
| F5 无反应 / launch.json 报错 `值不被接受。有效值: "grunt"` | `preLaunchTask` 引用的任务类型未注册。改用 **"ESP32-S3 调试 (cppdbg + OpenOCD)"** 配置（见上节） |
| 报错 `command 'extension.launchBinary' not found` | PlatformIO 扩展未激活或动态命令未解析。**重启 VSCode** 让扩展完全加载后再 F5；或改用 cppdbg 配置 |
| 报错 `OpenOCD: could not find or open device` | 开发板未连接 USB、或插的是 UART 口。插好**原生 USB 口**再试 |
| F5 后提示找不到任务 | 确认 `.vscode/tasks.json` 存在且 label 与 `launch.json` 的 `preLaunchTask` 完全一致（含空格/符号），改完**重启 VSCode** 生效 |
| 调试提示 `no device found` / OpenOCD 找不到设备 | USB 线不支持数据传输；或插的是 **UART 口**，请换到 **原生 USB 口** |
| 一直卡在 `Trying to connect...` | 检查 BOOT/EN 是否被按住；拔插 USB 后重试 |
| 断点无效 / 不命中 | 固件与源码不同步——先 `F5` 前确认已重新编译；或 `build_type` 用了 release 优化内联 |
| 看不到源码（显示汇编/反汇编） | 工程与源码路径不一致；确认 VSCode 打开的是 `ESP32` 文件夹 |
| 调试窗口提示 `Could not connect to target` | 重新插拔 USB；或先手动 `pio run -t upload` 烧录一次 |
| Windows 提示驱动问题 | Win10/11 自带 USB-Serial/JTAG (CDC) 驱动；若异常可在设备管理器中卸载设备后重新插拔 |
| 调试与串口监视端口冲突 | 不要同时用两个 USB 口监视；调试时关闭串口监视器 |
| PlatformIO 扩展有两个版本并存 | 在扩展面板卸载旧版（如 3.3.3），只保留最新版，然后重启 VSCode |

---

## 6. 常用调试技巧（ESP32 特供）

- **FreeRTOS 任务**：在 `vTaskDelay` 等位置打断点，配合"调用堆栈"观察任务调度。
- **外设寄存器**：调试器侧边栏可查看 `s_dev`、`s_bus` 等静态变量，确认 I2C 句柄是否初始化成功。
- **查看 IP5306 通讯结果**：在 `ip5306_scan_bus` / `print_status` 处下断点，单步观察 `found` 计数与 `ip5306_status_t` 字段。
- **日志过滤**：`monitor_speed` 已配 115200，调试时日志走 USB-Serial/JTAG，可在 PlatformIO 终端查看。
