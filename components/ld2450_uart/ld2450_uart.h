#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"

#include "ld2450_protocol.h"

#include <deque>
#include <initializer_list>
#include <vector>

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif

namespace esphome {
namespace ld2450_uart {

static const uint8_t MAX_TARGETS = ld2450_proto::MAX_TARGETS;

class LD2450UartComponent : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_throttle(uint32_t throttle_ms) { this->throttle_ = throttle_ms; }

#ifdef USE_SENSOR
  void set_x_sensor(uint8_t target, sensor::Sensor *s) { this->x_sensors_[target] = s; }
  void set_y_sensor(uint8_t target, sensor::Sensor *s) { this->y_sensors_[target] = s; }
  void set_speed_sensor(uint8_t target, sensor::Sensor *s) { this->speed_sensors_[target] = s; }
  void set_distance_sensor(uint8_t target, sensor::Sensor *s) { this->distance_sensors_[target] = s; }
  void set_angle_sensor(uint8_t target, sensor::Sensor *s) { this->angle_sensors_[target] = s; }
  void set_resolution_sensor(uint8_t target, sensor::Sensor *s) { this->resolution_sensors_[target] = s; }
  void set_target_count_sensor(sensor::Sensor *s) { this->target_count_sensor_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_presence_binary_sensor(binary_sensor::BinarySensor *s) { this->presence_binary_sensor_ = s; }
  void set_target_present_binary_sensor(uint8_t target, binary_sensor::BinarySensor *s) {
    this->target_present_binary_[target] = s;
  }
  void set_target_moving_binary_sensor(uint8_t target, binary_sensor::BinarySensor *s) {
    this->target_moving_binary_[target] = s;
  }
#endif
#ifdef USE_SWITCH
  void set_bluetooth_switch(switch_::Switch *s) { this->bluetooth_switch_ = s; }
  void set_multi_target_switch(switch_::Switch *s) { this->multi_target_switch_ = s; }
#endif

  // --- Configuration commands (UART command frames) ---
  // Each call queues one complete command sequence (atomically: all frames or
  // none). loop() sends one frame at a time and only advances to the next one
  // once the module acknowledged the previous frame; a rejected ACK or an ACK
  // timeout aborts the rest of the sequence and sends END_CONFIG so the module
  // never stays stuck in config mode. Nothing here blocks the main loop.
  // Every sequence enters config mode first and either leaves it again or ends
  // in a module restart followed by a re-read of the module state.
  // Note: the radar stops reporting target data while in config mode.
  void set_bluetooth(bool enable);
  void set_multi_target(bool enable);
  void restart_module();
  void factory_reset();
  // Query the MAC (= Bluetooth state) and the tracking mode and publish both to
  // the switches. Runs once after boot and after every restart.
  void read_all_info();

 protected:
  struct QueuedCommand {
    uint8_t command;
    uint8_t value[2];
    uint8_t value_len;
    uint16_t gap_ms;  // wait this long after the ACK before sending the next frame
  };

  static constexpr uint16_t COMMAND_GAP_MS = 50;
  static constexpr uint16_t RESTART_GAP_MS = 1500;  // module reboot time
  static constexpr uint16_t ACK_TIMEOUT_MS = 500;
  static constexpr size_t MAX_QUEUED_COMMANDS = 32;

  void process_buffer_();
  void handle_targets_(const ld2450_proto::Target targets[MAX_TARGETS], uint8_t count);
  void handle_ack_(const ld2450_proto::Ack &ack);
  void process_command_queue_();
  void abort_sequence_(const char *reason);
  void send_command_(uint8_t command, const uint8_t *value, uint8_t value_len);
  // Append a whole sequence, or nothing at all if it would not fit.
  bool enqueue_sequence_(std::initializer_list<QueuedCommand> sequence);
  static QueuedCommand cmd_(uint8_t command, uint16_t gap_ms = COMMAND_GAP_MS) {
    return QueuedCommand{command, {0x00, 0x00}, 0, gap_ms};
  }
  static QueuedCommand cmd_value_(uint8_t command, uint16_t value, uint16_t gap_ms = COMMAND_GAP_MS) {
    return QueuedCommand{command, {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)}, 2, gap_ms};
  }
  static QueuedCommand enter_config_() { return cmd_value_(ld2450_proto::CMD_ENABLE_CONFIG, 0x0001); }
  static QueuedCommand exit_config_() { return cmd_(ld2450_proto::CMD_END_CONFIG); }

  std::vector<uint8_t> buffer_;
  std::deque<QueuedCommand> command_queue_;
  QueuedCommand in_flight_{0, {0x00, 0x00}, 0, 0};
  bool waiting_ack_{false};
  uint32_t sent_at_{0};
  uint32_t next_command_at_{0};
  uint32_t throttle_{200};
  uint32_t last_publish_{0};
  bool pending_bluetooth_{false};

  // Last published target set, for change detection: every publish_state is an
  // API message to Home Assistant, so unchanged values are not re-sent.
  ld2450_proto::Target last_[MAX_TARGETS];
  int16_t last_count_{-1};
  bool have_last_{false};

#ifdef USE_SENSOR
  sensor::Sensor *x_sensors_[MAX_TARGETS]{nullptr, nullptr, nullptr};
  sensor::Sensor *y_sensors_[MAX_TARGETS]{nullptr, nullptr, nullptr};
  sensor::Sensor *speed_sensors_[MAX_TARGETS]{nullptr, nullptr, nullptr};
  sensor::Sensor *distance_sensors_[MAX_TARGETS]{nullptr, nullptr, nullptr};
  sensor::Sensor *angle_sensors_[MAX_TARGETS]{nullptr, nullptr, nullptr};
  sensor::Sensor *resolution_sensors_[MAX_TARGETS]{nullptr, nullptr, nullptr};
  sensor::Sensor *target_count_sensor_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *presence_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *target_present_binary_[MAX_TARGETS]{nullptr, nullptr, nullptr};
  binary_sensor::BinarySensor *target_moving_binary_[MAX_TARGETS]{nullptr, nullptr, nullptr};
#endif
#ifdef USE_SWITCH
  switch_::Switch *bluetooth_switch_{nullptr};
  switch_::Switch *multi_target_switch_{nullptr};
#endif
};

#ifdef USE_SWITCH
// The switches are deliberately not optimistic: the state is published only
// once the module acknowledged the command (and re-read from the module after
// the Bluetooth restart), so Home Assistant never shows a toggle the radar did
// not accept. Their initial state comes from read_all_info() after boot, which
// is why they use restore_mode DISABLED by default.
class LD2450BluetoothSwitch : public switch_::Switch, public Parented<LD2450UartComponent> {
 protected:
  void write_state(bool state) override { this->parent_->set_bluetooth(state); }
};

class LD2450MultiTargetSwitch : public switch_::Switch, public Parented<LD2450UartComponent> {
 protected:
  void write_state(bool state) override { this->parent_->set_multi_target(state); }
};
#endif

#ifdef USE_BUTTON
class LD2450RestartButton : public button::Button, public Parented<LD2450UartComponent> {
 protected:
  void press_action() override { this->parent_->restart_module(); }
};

class LD2450FactoryResetButton : public button::Button, public Parented<LD2450UartComponent> {
 protected:
  void press_action() override { this->parent_->factory_reset(); }
};
#endif

}  // namespace ld2450_uart
}  // namespace esphome
