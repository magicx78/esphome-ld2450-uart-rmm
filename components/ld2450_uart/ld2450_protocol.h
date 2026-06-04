#pragma once
//
// Pure, dependency-free implementation of the HLK-LD2450 serial protocol.
//
// This header intentionally has NO ESPHome dependencies (only the C++ standard
// library). That keeps the frame parser unit-testable on a normal host with a
// plain g++ build (see tests/test_parser.cpp) and decoupled from the ESP
// firmware. The ESPHome component (ld2450_uart.h/.cpp) includes this header and
// only adds the I/O glue + entity publishing on top.
//
// Protocol reference: HLK-LD2450 serial communication manual, cross-checked
// against the upstream ESPHome `ld2450` component decode logic.
//

#include <cstdint>
#include <cstddef>
#include <cmath>

namespace ld2450_proto {

// ---- Data (report) frame -------------------------------------------------
// Layout: AA FF 03 00 | 3 x (8 bytes per target) | 55 CC  => 30 bytes total.
static constexpr uint8_t DATA_HEADER[4] = {0xAA, 0xFF, 0x03, 0x00};
static constexpr uint8_t DATA_TAIL[2] = {0x55, 0xCC};
static constexpr uint8_t MAX_TARGETS = 3;
static constexpr size_t TARGET_SIZE = 8;
static constexpr size_t FRAME_LEN = 4 + MAX_TARGETS * TARGET_SIZE + 2;  // = 30

struct Target {
  int16_t x = 0;            // mm
  int16_t y = 0;            // mm
  int16_t speed = 0;        // mm/s
  uint16_t resolution = 0;  // distance resolution / gate size (mm)
  bool active = false;

  // Euclidean distance from the radar origin, in mm.
  float distance() const {
    return std::sqrt(static_cast<float>(x) * x + static_cast<float>(y) * y);
  }
};

// Coordinate decoding (matches upstream ESPHome ld2450):
//   value = (high & 0x7F) << 8 | low
//   the TOP bit of `high` is the sign flag: when it is CLEAR the value is
//   negative, when it is SET the value is positive.
inline int16_t decode_coordinate(uint8_t low, uint8_t high) {
  int16_t v = static_cast<int16_t>(((high & 0x7F) << 8) | low);
  if ((high & 0x80) == 0) {
    v = static_cast<int16_t>(-v);
  }
  return v;  // mm
}

// Speed uses the same sign convention; the raw value is in cm/s and is scaled
// by 10 to report mm/s (matches upstream ESPHome ld2450).
inline int16_t decode_speed(uint8_t low, uint8_t high) {
  int16_t v = static_cast<int16_t>(((high & 0x7F) << 8) | low);
  if ((high & 0x80) == 0) {
    v = static_cast<int16_t>(-v);
  }
  return static_cast<int16_t>(v * 10);  // mm/s
}

inline uint16_t decode_resolution(uint8_t low, uint8_t high) {
  return static_cast<uint16_t>((high << 8) | low);
}

inline bool has_data_header(const uint8_t *buf) {
  return buf[0] == DATA_HEADER[0] && buf[1] == DATA_HEADER[1] && buf[2] == DATA_HEADER[2] &&
         buf[3] == DATA_HEADER[3];
}

inline bool has_data_tail(const uint8_t *buf) {
  return buf[FRAME_LEN - 2] == DATA_TAIL[0] && buf[FRAME_LEN - 1] == DATA_TAIL[1];
}

// Validate and decode a complete 30-byte data frame.
//   out   - array of MAX_TARGETS targets, filled on success
//   count - number of *active* targets (a target is active when x or y != 0)
// Returns false if the buffer is too short or the header/tail do not match.
inline bool parse_data_frame(const uint8_t *buf, size_t len, Target out[MAX_TARGETS], uint8_t &count) {
  count = 0;
  if (buf == nullptr || len < FRAME_LEN) {
    return false;
  }
  if (!has_data_header(buf) || !has_data_tail(buf)) {
    return false;
  }
  for (uint8_t i = 0; i < MAX_TARGETS; i++) {
    const size_t s = 4 + static_cast<size_t>(i) * TARGET_SIZE;
    Target &t = out[i];
    t.x = decode_coordinate(buf[s], buf[s + 1]);
    t.y = decode_coordinate(buf[s + 2], buf[s + 3]);
    t.speed = decode_speed(buf[s + 4], buf[s + 5]);
    t.resolution = decode_resolution(buf[s + 6], buf[s + 7]);
    t.active = (t.x != 0 || t.y != 0);
    if (t.active) {
      count++;
    }
  }
  return true;
}

// ---- Command frame -------------------------------------------------------
// Layout: FD FC FB FA | len_lo len_hi | cmd_word(2, LE) | value... | 04 03 02 01
static constexpr uint8_t CMD_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static constexpr uint8_t CMD_FOOTER[4] = {0x04, 0x03, 0x02, 0x01};

// Command words (low byte; high byte is always 0x00 for the LD2450).
static constexpr uint8_t CMD_ENABLE_CONFIG = 0xFF;  // value 0x0001
static constexpr uint8_t CMD_END_CONFIG = 0xFE;
static constexpr uint8_t CMD_RESTART = 0xA3;
static constexpr uint8_t CMD_FACTORY_RESET = 0xA2;
static constexpr uint8_t CMD_BLUETOOTH = 0xA4;      // value 0x0001 on / 0x0000 off
static constexpr uint8_t CMD_SINGLE_TARGET = 0x80;
static constexpr uint8_t CMD_MULTI_TARGET = 0x90;
static constexpr uint8_t CMD_QUERY_VERSION = 0xA0;

}  // namespace ld2450_proto
