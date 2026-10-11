# ESP32-P4X-EYE → Wi-Fi JPEG → ROS2

P4X-EYE 使用 OV2710 摄像头、P4 硬件 JPEG 编码和板载 ESP32-C6 Wi-Fi。Ubuntu 上的 C++ 节点通过 HTTP 获取最新 JPEG，发布：

| 话题 | 类型 | 用途 |
| --- | --- | --- |
| /camera/image/compressed | sensor_msgs/msg/CompressedImage | JPEG 原始数据 |
| /camera/image | sensor_msgs/msg/Image | 按需解码的 BGR8 图像，直接供 rqt 显示 |

当前配置为 1280×720、JPEG 质量 65、最高编码帧率 10 FPS。实际帧率由摄像头时序和网络决定。HTTP 长连接重复请求最新图像，接口为 /capture.jpg；浏览器打开板卡 IP 根路径可预览。

## 本机快速启动

开发板目前地址为 http://192.168.31.53/。在本文件夹的两个终端分别运行：

~~~bash
./scripts/run_ros_camera.sh
~~~

~~~bash
./scripts/view_ros_camera.sh
~~~

如 DHCP 分配的地址改变，可把新地址作为参数传给接收脚本：

~~~bash
./scripts/run_ros_camera.sh http://新的IP/capture.jpg
~~~

P4 端 ESP-Hosted 固定为实板验证过的 3.0.9，组件版本记录在 dependencies.lock。板载 C6 固件为 2.12.12，启动时会提示主版本不同；当前图像传输已通过实测，未改写 C6 固件。

## 本机验证结果

已完成 P4 v3.2 实板烧录，写入哈希校验通过。最终固件接入 Wi-Fi 后，ROS2 集成验证收到了 1280×720 JPEG 和 BGR8 图像；rqt_image_view 已订阅 /camera/image。HTTP 503 和超大图像响应的恢复测试也已通过。

验证日志：.setup/logs/camera-flash.log、camera-boot.log、ros-real-camera-test.log、ros-integration-test.log。环境及固件摘要记录于 .setup/manifest.json。

复测真实摄像头：

~~~bash
source /opt/ros/jazzy/setup.bash
python3 tests/verify_ros_bridge.py --url http://192.168.31.53/capture.jpg
~~~

## ESP32 编译与烧录

~~~bash
cd /home/mark/projects/VScode
source ./activate.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
~~~

Ctrl+] 退出串口监视，Ctrl+Shift+B 在 VS Code 中编译。运行 idf.py menuconfig，在 P4X-EYE Camera Stream 下修改帧率和 JPEG 质量。

本工程适用于 ESP32-P4X-EYE（3.1 及更新芯片），已连接实物为 v3.2。将开发板 Debug USB 接口连接到 VMware 内的 Ubuntu。

Wi-Fi 名称和密码位于 main/wifi_credentials.h，已从版本管理中排除，文件权限为 600。修改后重新编译烧录。复制工程时参考 main/wifi_credentials.example.h 创建本地文件。

## 获取摄像头地址

连接成功后，串口输出 CAMERA_URL=http://<开发板IP>/capture.jpg。

Ubuntu 必须能访问该地址。VMware NAT 模式通常可以主动访问宿主机所在局域网；如果无法访问，请检查宿主机是否连接同一 Wi-Fi、路由器是否开启客户端隔离，并尝试 VMware 桥接模式。

## 编译 ROS2 C++ 节点

在普通终端中运行，避免使用 ESP-IDF Python 环境：

~~~bash
cd /home/mark/projects/VScode/ros2_ws
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --packages-select p4x_camera_bridge
source install/setup.bash
~~~

本机已有 ROS2 Jazzy、rclcpp、sensor_msgs、cv_bridge、OpenCV 和 libcurl 开发库。

## 启动接收和显示

将地址替换为串口输出的实际摄像头地址：

~~~bash
source /opt/ros/jazzy/setup.bash
source /home/mark/projects/VScode/ros2_ws/install/setup.bash
ros2 launch p4x_camera_bridge camera.launch.py camera_url:=http://<开发板IP>/capture.jpg
~~~

在另一个终端运行：

~~~bash
source /opt/ros/jazzy/setup.bash
ros2 run rqt_image_view rqt_image_view /camera/image
~~~

也可直接运行 C++ 节点：

~~~bash
ros2 run p4x_camera_bridge camera_bridge --ros-args \
  -p camera_url:=http://<开发板IP>/capture.jpg \
  -p fps:=10.0 -p publish_raw:=true
~~~

仅需压缩图像时设置 publish_raw:=false。节点仅在 /camera/image 存在订阅者时解码，以减少 CPU 开销。发布使用深度为 2 的可靠 QoS。

header.frame_id 默认为 camera_optical_frame。header.stamp 为 Ubuntu 收到 JPEG 的时间，并非同步后的曝光时间；当前没有标定 CameraInfo 或 TF。

## 故障排查

- 串口权限不足：执行 sudo setfacl -m u:mark:rw /dev/ttyACM0。USB 重插后可能需要重做。
- Wi-Fi 断开：固件自动重新连接，串口显示断开原因。
- 摄像头尚未准备好或停止输出：HTTP 返回 503，ROS 节点退避重试。
- SDIO/C6 超时：检查板载 C6 的 ESP-Hosted 固件；P4 的 Debug USB 不能直接作为 C6 烧录口。C6 有独立编程测试点。
- 摄像头初始化失败：检查排线和供电，观察 OV2710 检测日志。
- 本板 SDIO 引脚为 CMD=27、CLK=28、D0..D3=29..32、C6 EN=9，不同于 Function-EV-Board。
- USB 烧录中断时，按住 BOOT、按一下 RESET、松开 BOOT 后重试。

## 代码位置

- main/camera.c：电源、时钟、V4L2 采集、硬件 JPEG 编码、最新帧缓存。
- main/network.c：C6 远程 Wi-Fi 初始化、重连和地址输出。
- main/main.c：HTTP JPEG 接口及浏览器预览。
- ros2_ws/src/p4x_camera_bridge/src/camera_bridge.cpp：C++ HTTP 接收、ROS2 发布与按需解码。
- .setup/logs：编译、烧录及启动验证日志。

官方依据：[P4X-EYE 硬件指南](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4x-eye/user_guide.html)、[视频组件](https://github.com/espressif/esp-video-components)、[ESP-Hosted](https://github.com/espressif/esp-hosted-mcu)。

工具安装说明保存在 docs/ENVIRONMENT.md，其中原版 P4-EYE 示例说明是历史记录；本项目以当前 README 和 sdkconfig.defaults 为准。
