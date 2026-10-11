# ESP32-P4-EYE 开发工作区

已配置乐鑫官方 ESP-IDF v5.5.5、ESP32-P4 RISC-V GCC/GDB、OpenOCD、CMake、Ninja、独立 Python 环境，以及 VS Code 的 ESP-IDF、C/C++、CMake Tools 插件。框架和工具位于本文件夹的隐藏目录，不依赖系统 Python 的 pip/venv，也不需要管理员权限来编译。

## 开始开发

在 VS Code 中打开本文件夹，安装完成后运行命令面板中的 `Developer: Reload Window`。新建终端会自动加载本工作区的 ESP-IDF 环境。

在普通 Bash 终端中运行：

```bash
cd /home/mark/projects/VScode
source ./activate.sh
idf.py build
```

`Ctrl+Shift+B` 也可以构建。`终端 → 运行任务` 中提供 menuconfig、烧录和串口监视任务。

根目录是一个最小串口示例，入口为 `main/main.c`。它输出芯片版本和可用内存，方便先验证编译、下载和串口链路。

## 连接开发板

使用支持数据传输的 USB 线连接板上的 **USB Debug** 接口。在 VMware 菜单中将对应 USB 设备连接到 Ubuntu 虚拟机，再在 Ubuntu 中检查：

```bash
ls -l /dev/ttyACM* /dev/ttyUSB*
```

得到实际串口名称后烧录，例如：

```bash
source ./activate.sh
idf.py -p /dev/ttyACM0 flash monitor
```

按 `Ctrl+]` 退出串口监视。串口未识别时可按住 BOOT、按一下 RESET，再松开 BOOT，进入下载模式。

当前账户未加入 `dialout` 组。如果连接后提示串口权限不足，在自己的终端运行以下命令并注销后重新登录：

```bash
sudo usermod -aG dialout "$USER"
```

USB JTAG 调试配置已放在 `.vscode/launch.json`。如果 OpenOCD 报 USB 权限错误，可安装官方 udev 规则：

```bash
sudo cp .espressif/tools/openocd-esp32/*/openocd-esp32/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

之后重新插拔开发板。烧录和调试需要实板连接后验证。

## P4-EYE 与 P4X-EYE

本工程按用户指定的原版 **ESP32-P4-EYE** 配置，启用了 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`，支持 0.x/1.x 芯片。

如果实物是 **ESP32-P4X-EYE（3.x 芯片）**，运行 `idf.py menuconfig`，在 `Component config → Hardware Settings → Chip revision` 中关闭 `Select ESP32-P4 revisions <3.0`，并按实物选择最低芯片版本。两种芯片不能共用同一固件。先确认版本再烧录。

## 官方摄像头及屏幕示例

已下载到：

```text
.esp/esp-dev-kits/examples/esp32-p4-eye/examples/factory_demo
.esp/esp-dev-kits/examples/esp32-p4-eye/examples/common_components
```

在 VS Code 资源管理器中默认隐藏 `.esp`、`.espressif`、`.setup`，避免扫描大量工具文件；可从终端访问。

根目录基础工程用于验证开发环境。官方 factory_demo 包含相机、屏幕、SD 卡及 AI 功能，其托管组件会在首次配置该示例时下载。该示例的 SD 卡性能补丁依赖指定 IDF 提交，未对当前稳定版 IDF 自动应用；开发此示例时请先阅读它的 `README_CN.md`。

## 文件位置

| 位置 | 用途 |
| --- | --- |
| `.esp/esp-idf` | ESP-IDF v5.5.5 源码及子模块 |
| `.esp/esp-dev-kits` | 官方 P4-EYE 示例的稀疏检出 |
| `.espressif` | 工具链、调试器、构建工具、Python 环境 |
| `.setup/sysroot` | Ubuntu 官方包提取的本地开发依赖 |
| `.setup/logs` | 下载、安装和编译记录 |
| `.vscode` | 工作区插件设置、任务和调试入口 |
| `activate.sh` | 在当前 Bash 终端加载开发环境 |
| `sdkconfig.defaults` | 芯片、Flash 和控制台初始配置 |

工具按当前绝对路径配置；移动整个文件夹后，需要同步修改 `.vscode/settings.json` 和 `.espressif/esp_idf.json`。Python 虚拟环境也需要在新路径重新建立。

## 官方参考

- [ESP32-P4-EYE 硬件指南](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4-eye/user_guide.html)
- [ESP-IDF 5.5 Linux 安装指南](https://docs.espressif.com/projects/esp-idf/zh_CN/release-v5.5/esp32p4/get-started/linux-macos-setup.html)
- [P4-EYE factory_demo](https://github.com/espressif/esp-dev-kits/tree/master/examples/esp32-p4-eye/examples/factory_demo)
