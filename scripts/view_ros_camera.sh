#!/usr/bin/env bash
set -e
_p4_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source /opt/ros/jazzy/setup.bash
export ROS_LOG_DIR="${_p4_root}/.setup/logs/ros"
# Remove Snap VS Code GTK library overrides before starting the system Qt app.
unset GTK_PATH GTK_EXE_PREFIX GTK_IM_MODULE_FILE GDK_PIXBUF_MODULE_FILE GDK_PIXBUF_MODULEDIR GIO_MODULE_DIR
export QT_QPA_PLATFORM=xcb
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
unset ROS_LOCALHOST_ONLY
exec ros2 run rqt_image_view rqt_image_view /camera/image
