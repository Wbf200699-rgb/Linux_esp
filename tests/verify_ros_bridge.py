#!/usr/bin/env python3
"""End-to-end HTTP -> C++ bridge -> ROS compressed and raw image test."""
import argparse
import http.server
import os
from pathlib import Path
import signal
import subprocess
import threading
import time

import cv2
import numpy as np
import rclpy
from sensor_msgs.msg import CompressedImage, Image

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--url", help="Actual camera URL; omit to test with a fault-injecting HTTP server")
args = parser.parse_args()
server = None
expected = None
if not args.url:
    fixture = np.zeros((48, 64, 3), dtype=np.uint8)
    fixture[:, :32] = [20, 60, 200]
    fixture[:, 32:] = [200, 180, 30]
    ok, encoded = cv2.imencode(".jpg", fixture)
    assert ok
    expected = encoded.tobytes()

    class Handler(http.server.BaseHTTPRequestHandler):
        requests = 0

        def do_GET(self):
            Handler.requests += 1
            if Handler.requests <= 2:
                body, code, content_type = b"warming up", 503, "text/plain"
            elif Handler.requests == 3:
                body, code, content_type = b"\xff\xd8" + b"x" * (1024 * 1024), 200, "image/jpeg"
            else:
                body, code, content_type = expected, 200, "image/jpeg"
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    args.url = f"http://127.0.0.1:{server.server_port}/capture.jpg"

os.environ["ROS_DOMAIN_ID"] = "73"
os.environ["ROS_LOCALHOST_ONLY"] = "1"
os.environ["ROS_LOG_DIR"] = str(ROOT / ".setup/logs/ros")
rclpy.init()
node = rclpy.create_node("verify_p4x_camera")
compressed, raw = [], []
subscriptions = [
    node.create_subscription(CompressedImage, "/camera/image/compressed", compressed.append, 10),
    node.create_subscription(Image, "/camera/image", raw.append, 10),
]
log = (ROOT / ".setup/logs/ros-verification-node.log").open("w")
process = subprocess.Popen([
    str(ROOT / "ros2_ws/install/p4x_camera_bridge/lib/p4x_camera_bridge/camera_bridge"),
    "--ros-args", "-p", f"camera_url:={args.url}",
], stdout=log, stderr=subprocess.STDOUT, env=os.environ.copy())
try:
    deadline = time.monotonic() + 25
    while time.monotonic() < deadline and (len(compressed) < 10 or len(raw) < 5):
        rclpy.spin_once(node, timeout_sec=0.1)
        assert process.poll() is None, "C++ bridge exited unexpectedly"
    assert len(compressed) >= 10 and len(raw) >= 5, (len(compressed), len(raw))
    msg = compressed[-1]
    assert msg.format == "jpeg" and msg.header.frame_id == "camera_optical_frame"
    image = cv2.imdecode(np.frombuffer(bytes(msg.data), np.uint8), cv2.IMREAD_COLOR)
    assert image is not None
    assert raw[-1].encoding == "bgr8"
    assert (raw[-1].height, raw[-1].width) == image.shape[:2]
    if expected is not None:
        assert bytes(msg.data) == expected, "Compressed JPEG payload changed in transit"
        raw_pixels = np.frombuffer(bytes(raw[-1].data), np.uint8).reshape(image.shape)
        assert np.array_equal(raw_pixels, image), "Raw ROS image differs from JPEG decode"
        print("PASS: recovers from HTTP 503 and oversized JPEG; publishes unchanged JPEG and correct BGR8")
    else:
        (ROOT / ".setup/received.jpg").write_bytes(bytes(msg.data))
        print(f"PASS: real camera {image.shape[1]}x{image.shape[0]}, "
              f"received {len(compressed)} JPEG and {len(raw)} raw messages, {len(msg.data)} bytes")
finally:
    process.send_signal(signal.SIGINT)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    log.close()
    node.destroy_node()
    rclpy.shutdown()
    if server:
        server.shutdown()
