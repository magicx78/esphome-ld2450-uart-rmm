#include "ld2450_uart.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace ld2450_uart {

static const char *const TAG = "ld2450_uart";

void LD2450UartComponent::setup() {
  this->buffer_.reserve(64);
  ESP_LOGCONFIG(TAG, "Setting up LD2450 UART...");
}

void LD2450UartComponent::loop() {
  while (this->available()) {
    uint8_t c;
    if (!this->read_byte(&c)) {
      break;
    }
    this->buffer_.push_back(c);

    // Resync: drop bytes until the buffer starts with the data-frame header.
    while (!this->buffer_.empty() && this->buffer_[0] != ld2450_proto::DATA_HEADER[0]) {
      this->buffer_.erase(this->buffer_.begin());
    }

    // Hard cap so a stream without a valid tail can never grow unbounded.
    if (this->buffer_.size() > 256) {
      this->buffer_.erase(this->buffer_.begin());
      continue;
    }

    if (this->buffer_.size() < ld2450_proto::FRAME_LEN) {
      continue;
    }

    // We have at least one full frame's worth of bytes starting with 0xAA.
    if (!ld2450_proto::has_data_header(this->buffer_.data())) {
      // First byte was 0xAA but the rest of the header does not match.
      this->buffer_.erase(this->buffer_.begin());
      continue;
    }

    ld2450_proto::Target targets[MAX_TARGETS];
    uint8_t count = 0;
    if (ld2450_proto::parse_data_frame(this->buffer_.data(), ld2450_proto::FRAME_LEN, targets, count)) {
      this->handle_targets_(targets, count);
      this->buffer_.erase(this->buffer_.begin(),
                          this->buffer_.begin() + static_cast<long>(ld2450_proto::FRAME_LEN));
    } else {
      // Header matched but tail did not -> misaligned, drop one byte and retry.
      this->buffer_.erase(this->buffer_.begin());
    }
  }
}

void LD2450UartComponent::handle_targets_(const ld2450_proto::Target targets[MAX_TARGETS], uint8_t count) {
  const uint32_t now = millis();
  if (this->throttle_ > 0 && (now - this->last_publish_) < this->throttle_) {
    return;
  }
  this->last_publish_ = now;

  for (uint8_t i = 0; i < MAX_TARGETS; i++) {
    const ld2450_proto::Target &t = targets[i];
#ifdef USE_SENSOR
    if (this->x_sensors_[i] != nullptr) {
      this->x_sensors_[i]->publish_state(t.x);
    }
    if (this->y_sensors_[i] != nullptr) {
      this->y_sensors_[i]->publish_state(t.y);
    }
    if (this->speed_sensors_[i] != nullptr) {
      this->speed_sensors_[i]->publish_state(t.speed);
    }
    if (this->distance_sensors_[i] != nullptr) {
      this->distance_sensors_[i]->publish_state(t.distance());
    }
    if (this->angle_sensors_[i] != nullptr) {
      this->angle_sensors_[i]->publish_state(t.angle());
    }
    if (this->resolution_sensors_[i] != nullptr) {
      this->resolution_sensors_[i]->publish_state(t.resolution);
    }
#endif
#ifdef USE_BINARY_SENSOR
    if (this->target_present_binary_[i] != nullptr) {
      this->target_present_binary_[i]->publish_state(t.active);
    }
    if (this->target_moving_binary_[i] != nullptr) {
      this->target_moving_binary_[i]->publish_state(t.moving());
    }
#endif
  }

#ifdef USE_SENSOR
  if (this->target_count_sensor_ != nullptr) {
    this->target_count_sensor_->publish_state(count);
  }
#endif
#ifdef USE_BINARY_SENSOR
  if (this->presence_binary_sensor_ != nullptr) {
    this->presence_binary_sensor_->publish_state(count > 0);
  }
#endif
}

void LD2450UartComponent::send_command_(uint8_t command, const uint8_t *value, uint8_t value_len) {
  this->write_array(ld2450_proto::CMD_HEADER, sizeof(ld2450_proto::CMD_HEADER));
  const uint8_t len = static_cast<uint8_t>(2 + (value != nullptr ? value_len : 0));
  const uint8_t len_cmd[4] = {len, 0x00, command, 0x00};
  this->write_array(len_cmd, sizeof(len_cmd));
  if (value != nullptr && value_len > 0) {
    this->write_array(value, value_len);
  }
  this->write_array(ld2450_proto::CMD_FOOTER, sizeof(ld2450_proto::CMD_FOOTER));
  this->flush();
}

void LD2450UartComponent::enter_config_() {
  const uint8_t value[2] = {0x01, 0x00};
  this->send_command_(ld2450_proto::CMD_ENABLE_CONFIG, value, sizeof(value));
  delay(50);
}

void LD2450UartComponent::exit_config_() {
  this->send_command_(ld2450_proto::CMD_END_CONFIG, nullptr, 0);
  delay(50);
}

void LD2450UartComponent::set_bluetooth(bool enable) {
  ESP_LOGI(TAG, "Setting Bluetooth %s (module restarts to apply)", ONOFF(enable));
  this->enter_config_();
  const uint8_t value[2] = {static_cast<uint8_t>(enable ? 0x01 : 0x00), 0x00};
  this->send_command_(ld2450_proto::CMD_BLUETOOTH, value, sizeof(value));
  // The Bluetooth on/off setting only takes effect after a module restart.
  this->send_command_(ld2450_proto::CMD_RESTART, nullptr, 0);
}

void LD2450UartComponent::set_multi_target(bool enable) {
  ESP_LOGI(TAG, "Setting multi-target tracking %s", ONOFF(enable));
  this->enter_config_();
  this->send_command_(enable ? ld2450_proto::CMD_MULTI_TARGET : ld2450_proto::CMD_SINGLE_TARGET, nullptr, 0);
  this->exit_config_();
}

void LD2450UartComponent::restart_module() {
  ESP_LOGI(TAG, "Restarting LD2450 module");
  this->enter_config_();
  this->send_command_(ld2450_proto::CMD_RESTART, nullptr, 0);
  this->exit_config_();
}

void LD2450UartComponent::factory_reset() {
  ESP_LOGW(TAG, "Factory-resetting LD2450 module");
  this->enter_config_();
  this->send_command_(ld2450_proto::CMD_FACTORY_RESET, nullptr, 0);
  this->exit_config_();
}

void LD2450UartComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LD2450 UART:");
  ESP_LOGCONFIG(TAG, "  Throttle: %u ms", this->throttle_);
#ifdef USE_SENSOR
  for (uint8_t i = 0; i < MAX_TARGETS; i++) {
    LOG_SENSOR("  ", "Target X", this->x_sensors_[i]);
    LOG_SENSOR("  ", "Target Y", this->y_sensors_[i]);
    LOG_SENSOR("  ", "Target Speed", this->speed_sensors_[i]);
    LOG_SENSOR("  ", "Target Distance", this->distance_sensors_[i]);
    LOG_SENSOR("  ", "Target Resolution", this->resolution_sensors_[i]);
  }
  LOG_SENSOR("  ", "Target Count", this->target_count_sensor_);
#endif
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Presence", this->presence_binary_sensor_);
#endif
  this->check_uart_settings(256000);
}

}  // namespace ld2450_uart
}  // namespace esphome
