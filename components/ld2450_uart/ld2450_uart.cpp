#include "ld2450_uart.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace ld2450_uart {

namespace proto = ld2450_proto;

static const char *const TAG = "ld2450_uart";

void LD2450UartComponent::setup() {
  this->buffer_.reserve(64);
  ESP_LOGCONFIG(TAG, "Setting up LD2450 UART...");
  // Read Bluetooth / tracking state once the module has settled after boot.
  this->read_all_info();
  this->next_command_at_ = millis() + 1000;
}

void LD2450UartComponent::loop() {
  while (this->available()) {
    uint8_t c;
    if (!this->read_byte(&c)) {
      break;
    }
    this->buffer_.push_back(c);
    this->process_buffer_();
  }
  this->process_command_queue_();
}

// Consume complete frames from the front of the buffer. Two frame types share
// the line: 30-byte data frames (0xAA ...) and variable-length command ACKs
// (0xFD ...). Anything else is dropped byte by byte until a header byte is
// found. Both parsers reject over-long frames, so the buffer stays bounded.
void LD2450UartComponent::process_buffer_() {
  while (!this->buffer_.empty()) {
    const uint8_t first = this->buffer_[0];
    if (first != proto::DATA_HEADER[0] && first != proto::CMD_HEADER[0]) {
      this->buffer_.erase(this->buffer_.begin());
      continue;
    }

    if (first == proto::DATA_HEADER[0]) {
      if (this->buffer_.size() < proto::FRAME_LEN) {
        return;  // wait for the rest of the frame
      }
      proto::Target targets[MAX_TARGETS];
      uint8_t count = 0;
      if (proto::parse_data_frame(this->buffer_.data(), proto::FRAME_LEN, targets, count)) {
        this->handle_targets_(targets, count);
        this->buffer_.erase(this->buffer_.begin(),
                            this->buffer_.begin() + static_cast<long>(proto::FRAME_LEN));
      } else {
        // 0xAA but header/tail do not match -> misaligned, drop one byte and retry.
        this->buffer_.erase(this->buffer_.begin());
      }
      continue;
    }

    proto::Ack ack;
    const int consumed = proto::parse_ack_frame(this->buffer_.data(), this->buffer_.size(), ack);
    if (consumed > 0) {
      this->handle_ack_(ack);
      this->buffer_.erase(this->buffer_.begin(), this->buffer_.begin() + consumed);
    } else if (consumed == 0) {
      return;  // incomplete ACK, wait for more bytes
    } else {
      this->buffer_.erase(this->buffer_.begin());
    }
  }
}

void LD2450UartComponent::handle_targets_(const proto::Target targets[MAX_TARGETS], uint8_t count) {
  const uint32_t now = millis();
  // The throttle only paces the numeric sensors. Binary sensors and the target
  // count are cheap (they de-duplicate) and must not miss a frame in which a
  // target appears or disappears.
  const bool publish_numeric =
      !this->have_last_ || this->throttle_ == 0 || (now - this->last_publish_) >= this->throttle_;

#ifdef USE_BINARY_SENSOR
  for (uint8_t i = 0; i < MAX_TARGETS; i++) {
    if (this->target_present_binary_[i] != nullptr) {
      this->target_present_binary_[i]->publish_state(targets[i].active);
    }
    if (this->target_moving_binary_[i] != nullptr) {
      this->target_moving_binary_[i]->publish_state(targets[i].moving());
    }
  }
  if (this->presence_binary_sensor_ != nullptr) {
    this->presence_binary_sensor_->publish_state(count > 0);
  }
#endif
#ifdef USE_SENSOR
  if (this->target_count_sensor_ != nullptr && count != this->last_count_) {
    this->target_count_sensor_->publish_state(count);
  }
#endif
  this->last_count_ = count;

  if (!publish_numeric) {
    return;
  }

#ifdef USE_SENSOR
  for (uint8_t i = 0; i < MAX_TARGETS; i++) {
    const proto::Target &t = targets[i];
    const proto::Target &l = this->last_[i];
    const bool first = !this->have_last_;
    const bool x_changed = first || t.x != l.x;
    const bool y_changed = first || t.y != l.y;
    if (this->x_sensors_[i] != nullptr && x_changed) {
      this->x_sensors_[i]->publish_state(t.x);
    }
    if (this->y_sensors_[i] != nullptr && y_changed) {
      this->y_sensors_[i]->publish_state(t.y);
    }
    if (this->speed_sensors_[i] != nullptr && (first || t.speed != l.speed)) {
      this->speed_sensors_[i]->publish_state(t.speed);
    }
    if (this->distance_sensors_[i] != nullptr && (x_changed || y_changed)) {
      this->distance_sensors_[i]->publish_state(t.distance());
    }
    if (this->angle_sensors_[i] != nullptr && (x_changed || y_changed)) {
      this->angle_sensors_[i]->publish_state(t.angle());
    }
    if (this->resolution_sensors_[i] != nullptr && (first || t.resolution != l.resolution)) {
      this->resolution_sensors_[i]->publish_state(t.resolution);
    }
  }
#endif

  for (uint8_t i = 0; i < MAX_TARGETS; i++) {
    this->last_[i] = targets[i];
  }
  this->have_last_ = true;
  this->last_publish_ = now;
}

void LD2450UartComponent::handle_ack_(const proto::Ack &ack) {
  if (!ack.ok()) {
    ESP_LOGW(TAG, "Command 0x%02X rejected by module (status 0x%04X)", ack.command, ack.status);
    return;
  }
  switch (ack.command) {
    case proto::CMD_ENABLE_CONFIG:
      ESP_LOGV(TAG, "Config mode entered");
      break;
    case proto::CMD_END_CONFIG:
      ESP_LOGV(TAG, "Config mode left");
      break;
    case proto::CMD_BLUETOOTH:
      ESP_LOGD(TAG, "Bluetooth %s accepted, module restarts to apply", ONOFF(this->pending_bluetooth_));
#ifdef USE_SWITCH
      if (this->bluetooth_switch_ != nullptr) {
        this->bluetooth_switch_->publish_state(this->pending_bluetooth_);
      }
#endif
      break;
    case proto::CMD_QUERY_MAC: {
      const bool on = proto::mac_reply_means_bluetooth_on(ack);
      if (on) {
        ESP_LOGD(TAG, "Bluetooth on, MAC %02X:%02X:%02X:%02X:%02X:%02X", ack.data[0], ack.data[1], ack.data[2],
                 ack.data[3], ack.data[4], ack.data[5]);
      } else {
        ESP_LOGD(TAG, "Bluetooth off");
      }
#ifdef USE_SWITCH
      if (this->bluetooth_switch_ != nullptr) {
        this->bluetooth_switch_->publish_state(on);
      }
#endif
      break;
    }
    case proto::CMD_SINGLE_TARGET:
    case proto::CMD_MULTI_TARGET: {
      const bool multi = ack.command == proto::CMD_MULTI_TARGET;
      ESP_LOGD(TAG, "%s-target tracking accepted", multi ? "Multi" : "Single");
#ifdef USE_SWITCH
      if (this->multi_target_switch_ != nullptr) {
        this->multi_target_switch_->publish_state(multi);
      }
#endif
      break;
    }
    case proto::CMD_QUERY_TARGET_MODE:
      if (ack.data_len >= 1) {
        const bool multi = ack.data[0] == 0x02;
        ESP_LOGD(TAG, "Tracking mode: %s-target", multi ? "multi" : "single");
#ifdef USE_SWITCH
        if (this->multi_target_switch_ != nullptr) {
          this->multi_target_switch_->publish_state(multi);
        }
#endif
      }
      break;
    case proto::CMD_RESTART:
      ESP_LOGD(TAG, "Restart accepted, module rebooting");
      break;
    case proto::CMD_FACTORY_RESET:
      ESP_LOGD(TAG, "Factory reset accepted, applied by the following restart");
      break;
    default:
      ESP_LOGV(TAG, "ACK for command 0x%02X (%u data bytes)", ack.command, static_cast<unsigned>(ack.data_len));
      break;
  }
}

// --- Command queue --------------------------------------------------------

void LD2450UartComponent::enqueue_(uint8_t command, uint16_t gap_ms) {
  if (this->command_queue_.size() >= MAX_QUEUED_COMMANDS) {
    ESP_LOGW(TAG, "Command queue full, dropping command 0x%02X", command);
    return;
  }
  this->command_queue_.push_back(QueuedCommand{command, {0x00, 0x00}, 0, gap_ms});
}

void LD2450UartComponent::enqueue_value_(uint8_t command, uint16_t value, uint16_t gap_ms) {
  if (this->command_queue_.size() >= MAX_QUEUED_COMMANDS) {
    ESP_LOGW(TAG, "Command queue full, dropping command 0x%02X", command);
    return;
  }
  this->command_queue_.push_back(
      QueuedCommand{command, {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)}, 2, gap_ms});
}

void LD2450UartComponent::process_command_queue_() {
  if (this->command_queue_.empty()) {
    return;
  }
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - this->next_command_at_) < 0) {
    return;
  }
  const QueuedCommand q = this->command_queue_.front();
  this->command_queue_.pop_front();
  this->send_command_(q.command, q.value_len > 0 ? q.value : nullptr, q.value_len);
  this->next_command_at_ = now + q.gap_ms;
}

void LD2450UartComponent::send_command_(uint8_t command, const uint8_t *value, uint8_t value_len) {
  this->write_array(proto::CMD_HEADER, sizeof(proto::CMD_HEADER));
  const uint8_t len = static_cast<uint8_t>(2 + (value != nullptr ? value_len : 0));
  const uint8_t len_cmd[4] = {len, 0x00, command, 0x00};
  this->write_array(len_cmd, sizeof(len_cmd));
  if (value != nullptr && value_len > 0) {
    this->write_array(value, value_len);
  }
  this->write_array(proto::CMD_FOOTER, sizeof(proto::CMD_FOOTER));
  this->flush();
}

// After a restart the module boots in normal (reporting) mode, so the re-read
// enters config mode again on its own and leaves it at the end.
void LD2450UartComponent::enqueue_restart_and_reread_() {
  this->enqueue_(proto::CMD_RESTART, RESTART_GAP_MS);
  this->read_all_info();
}

void LD2450UartComponent::read_all_info() {
  this->enqueue_enter_config_();
  this->enqueue_value_(proto::CMD_QUERY_MAC, 0x0001);
  this->enqueue_(proto::CMD_QUERY_TARGET_MODE);
  this->enqueue_exit_config_();
}

void LD2450UartComponent::set_bluetooth(bool enable) {
  ESP_LOGI(TAG, "Setting Bluetooth %s (module restarts to apply)", ONOFF(enable));
  this->pending_bluetooth_ = enable;
  this->enqueue_enter_config_();
  this->enqueue_value_(proto::CMD_BLUETOOTH, enable ? 0x0001 : 0x0000);
  // The setting only takes effect after a module restart; the re-read
  // afterwards publishes the state the module actually came back with.
  this->enqueue_restart_and_reread_();
}

void LD2450UartComponent::set_multi_target(bool enable) {
  ESP_LOGI(TAG, "Setting multi-target tracking %s", ONOFF(enable));
  this->enqueue_enter_config_();
  this->enqueue_(enable ? proto::CMD_MULTI_TARGET : proto::CMD_SINGLE_TARGET);
  this->enqueue_(proto::CMD_QUERY_TARGET_MODE);
  this->enqueue_exit_config_();
}

void LD2450UartComponent::restart_module() {
  ESP_LOGI(TAG, "Restarting LD2450 module");
  this->enqueue_enter_config_();
  this->enqueue_restart_and_reread_();
}

void LD2450UartComponent::factory_reset() {
  ESP_LOGW(TAG, "Factory-resetting LD2450 module (restarts to apply)");
  this->enqueue_enter_config_();
  this->enqueue_(proto::CMD_FACTORY_RESET);
  // Per the protocol manual the defaults only take effect after a restart.
  this->enqueue_restart_and_reread_();
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
    LOG_SENSOR("  ", "Target Angle", this->angle_sensors_[i]);
    LOG_SENSOR("  ", "Target Resolution", this->resolution_sensors_[i]);
  }
  LOG_SENSOR("  ", "Target Count", this->target_count_sensor_);
#endif
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Presence", this->presence_binary_sensor_);
  for (uint8_t i = 0; i < MAX_TARGETS; i++) {
    LOG_BINARY_SENSOR("  ", "Target Present", this->target_present_binary_[i]);
    LOG_BINARY_SENSOR("  ", "Target Moving", this->target_moving_binary_[i]);
  }
#endif
#ifdef USE_SWITCH
  LOG_SWITCH("  ", "Bluetooth", this->bluetooth_switch_);
  LOG_SWITCH("  ", "Multi-Target", this->multi_target_switch_);
#endif
  this->check_uart_settings(256000);
}

}  // namespace ld2450_uart
}  // namespace esphome
