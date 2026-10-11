#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <curl/curl.h>
#include <opencv2/imgcodecs.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>

using namespace std::chrono_literals;

class CameraBridge : public rclcpp::Node
{
public:
  CameraBridge() : Node("p4x_camera_bridge")
  {
    url_ = declare_parameter<std::string>("camera_url", "http://192.168.1.100/capture.jpg");
    frame_id_ = declare_parameter<std::string>("frame_id", "camera_optical_frame");
    fps_ = declare_parameter<double>("fps", 10.0);
    publish_raw_ = declare_parameter<bool>("publish_raw", true);
    if (!std::isfinite(fps_) || fps_ <= 0.0 || fps_ > 25.0) {
      throw std::invalid_argument("fps must be in (0, 25]");
    }
    if (url_.rfind("http://", 0) != 0 && url_.rfind("https://", 0) != 0) {
      throw std::invalid_argument("camera_url must start with http:// or https://");
    }
    compressed_ = create_publisher<sensor_msgs::msg::CompressedImage>(
      "/camera/image/compressed", rclcpp::QoS(2));
    if (publish_raw_) {
      raw_ = create_publisher<sensor_msgs::msg::Image>("/camera/image", rclcpp::QoS(2));
    }
    RCLCPP_INFO(get_logger(), "Reading %s -> /camera/image/compressed", url_.c_str());
    worker_ = std::thread(&CameraBridge::receive_loop, this);
  }

  ~CameraBridge() override
  {
    stop_.store(true);
    if (worker_.joinable()) worker_.join();
  }

private:
  struct Download
  {
    std::vector<uint8_t> bytes;
    static constexpr size_t limit = 1024 * 1024;
  };

  static size_t write_data(char *data, size_t size, size_t count, void *opaque) noexcept
  {
    auto &download = *static_cast<Download *>(opaque);
    if (size != 0 && count > Download::limit / size) return 0;
    const size_t length = size * count;
    if (length > Download::limit - download.bytes.size()) return 0;
    try {
      const auto *first = reinterpret_cast<const uint8_t *>(data);
      download.bytes.insert(download.bytes.end(), first, first + length);
      return length;
    } catch (...) {
      return 0;
    }
  }

  static int progress(void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept
  {
    return static_cast<CameraBridge *>(opaque)->stop_.load() || !rclcpp::ok();
  }

  void pause(std::chrono::steady_clock::duration duration)
  {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (!stop_.load() && rclcpp::ok() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(10ms);
    }
  }

  void receive_loop()
  {
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) {
      RCLCPP_ERROR(get_logger(), "Cannot initialize libcurl");
      return;
    }
    curl_easy_setopt(curl.get(), CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &CameraBridge::write_data);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 1500L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 2500L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROXY, "*");
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, &CameraBridge::progress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(1.0 / fps_));
    unsigned frames = 0;
    auto backoff = 500ms;
    Download download;
    download.bytes.reserve(128 * 1024);
    while (!stop_.load() && rclcpp::ok()) {
      const auto started = std::chrono::steady_clock::now();
      download.bytes.clear();
      curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &download);
      const auto result = curl_easy_perform(curl.get());
      long status = 0;
      char *type = nullptr;
      curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
      curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_TYPE, &type);
      if (stop_.load() || !rclcpp::ok()) break;
      const auto &data = download.bytes;
      if (result != CURLE_OK || status != 200 || !type ||
          std::string(type).rfind("image/jpeg", 0) != 0 ||
          data.size() < 4 || data[0] != 0xff || data[1] != 0xd8) {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
          "Camera unavailable: HTTP %ld, curl=%s; retrying", status, curl_easy_strerror(result));
        pause(backoff);
        backoff = std::min(backoff * 2, 5000ms);
        continue;
      }
      backoff = 500ms;
      sensor_msgs::msg::CompressedImage message;
      // Receipt time: the ESP monotonic capture clock is not synchronized to ROS.
      message.header.stamp = now();
      message.header.frame_id = frame_id_;
      message.format = "jpeg";
      message.data = data;
      if (raw_ && raw_->get_subscription_count() > 0) {
        try {
          cv::Mat decoded = cv::imdecode(message.data, cv::IMREAD_COLOR);
          if (!decoded.empty()) {
            raw_->publish(*cv_bridge::CvImage(message.header, "bgr8", decoded).toImageMsg());
          } else {
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "JPEG decode failed");
          }
        } catch (const cv::Exception &error) {
          RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "Decode: %s", error.what());
        }
      }
      compressed_->publish(message);
      if (++frames == 1 || frames % 100 == 0) {
        RCLCPP_INFO(get_logger(), "Published JPEG %u (%zu bytes)", frames, message.data.size());
      }
      const auto remaining = period - (std::chrono::steady_clock::now() - started);
      if (remaining > std::chrono::steady_clock::duration::zero()) pause(remaining);
    }
  }

  std::string url_, frame_id_;
  double fps_;
  bool publish_raw_;
  std::atomic<bool> stop_{false};
  std::thread worker_;
  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr compressed_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr raw_;
};

int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 1;
  int result = 0;
  try {
    auto node = std::make_shared<CameraBridge>();
    rclcpp::spin(node);
    node.reset();
  } catch (const std::exception &error) {
    RCLCPP_ERROR(rclcpp::get_logger("p4x_camera_bridge"), "%s", error.what());
    result = 1;
  }
  rclcpp::shutdown();
  curl_global_cleanup();
  return result;
}
