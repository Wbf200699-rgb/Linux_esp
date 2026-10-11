#!/usr/bin/env bash
set -e
_p4_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source /opt/ros/jazzy/setup.bash
source "${_p4_root}/ros2_ws/install/setup.bash"
export ROS_LOG_DIR="${_p4_root}/.setup/logs/ros"
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
unset ROS_LOCALHOST_ONLY
exec ros2 run p4x_camera_bridge camera_bridge --ros-args \
    -p "camera_url:=${1:-http://192.168.31.53/capture.jpg}" \
    -p fps:=10.0 -p publish_raw:=true
