#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"

#include "ld2450_protocol.h"

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

  // --- Configuration commands (UART command frames) ---
  // Each command enters config mode, sends the command, then exits config mode.
  // Note: the radar stops reporting target data while in config mode.
  void set_bluetooth(bool enable);
  void set_multi_target(bool enable);
  void restart_module();
  void factory_reset();

 protected:
  void handle_targets_(const ld2450_proto::Target targets[MAX_TARGETS], uint8_t count);
  void send_command_(uint8_t command, const uint8_t *value, uint8_t value_len);
  void enter_config_();
  void exit_config_();

  std::vector<uint8_t> buffer_;
  uint32_t throttle_{200};
  uint32_t last_publish_{0};

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
};

#ifdef USE_SWITCH
class LD2450BluetoothSwitch : public switch_::Switch, public Parented<LD2450UartComponent> {
 protected:
  void write_state(bool state) override {
    this->parent_->set_bluetooth(state);
    this->publish_state(state);
  }
};

class LD2450MultiTargetSwitch : public switch_::Switch, public Parented<LD2450UartComponent> {
 protected:
  void write_state(bool state) override {
    this->parent_->set_multi_target(state);
    this->publish_state(state);
  }
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
