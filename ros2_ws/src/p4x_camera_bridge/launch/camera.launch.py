from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('camera_url', default_value='http://192.168.31.53/capture.jpg'),
        DeclareLaunchArgument('fps', default_value='10.0'),
        Node(
            package='p4x_camera_bridge',
            executable='camera_bridge',
            output='screen',
            parameters=[{
                'camera_url': LaunchConfiguration('camera_url'),
                'fps': ParameterValue(LaunchConfiguration('fps'), value_type=float),
                'publish_raw': True,
            }],
        ),
    ])
