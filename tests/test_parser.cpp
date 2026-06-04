// Host unit test for the LD2450 frame parser.
//
// Builds and runs WITHOUT ESPHome or any ESP hardware:
//
//   g++ -std=c++17 -I../components/ld2450_uart tests/test_parser.cpp -o test_parser
//   ./test_parser
//
// It feeds simulated LD2450 data frames into ld2450_proto::parse_data_frame and
// asserts the decoded x / y / speed / resolution values, the active-target flag
// and the target count. Frames are constructed with the inverse of the decode
// rule, so the test exercises the exact byte order and sign-bit handling.

#include "ld2450_protocol.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using ld2450_proto::Target;

static int g_checks = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    g_checks++;                                                           \
    if (!(cond)) {                                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);                 \
      return 1;                                                           \
    }                                                                     \
  } while (0)

// Encode a signed coordinate (mm) into {low, high} using the LD2450 convention:
//   top bit of `high` SET  -> positive value, magnitude in the low 15 bits
//   top bit of `high` CLEAR -> negative value, magnitude in the low 15 bits
static void encode_signed(int value, uint8_t &low, uint8_t &high) {
  int magnitude = value < 0 ? -value : value;
  low = static_cast<uint8_t>(magnitude & 0xFF);
  high = static_cast<uint8_t>((magnitude >> 8) & 0x7F);
  if (value >= 0) {
    high |= 0x80;
  }
}

struct RawTarget {
  int x_mm;
  int y_mm;
  int speed_raw;  // cm/s (decoded value is *10 -> mm/s)
  int resolution;
  bool empty;
};

static std::vector<uint8_t> build_frame(const RawTarget t[3]) {
  std::vector<uint8_t> f;
  for (int i = 0; i < 4; i++) f.push_back(ld2450_proto::DATA_HEADER[i]);
  for (int i = 0; i < 3; i++) {
    if (t[i].empty) {
      for (int b = 0; b < 8; b++) f.push_back(0x00);
      continue;
    }
    uint8_t lo, hi;
    encode_signed(t[i].x_mm, lo, hi);
    f.push_back(lo);
    f.push_back(hi);
    encode_signed(t[i].y_mm, lo, hi);
    f.push_back(lo);
    f.push_back(hi);
    encode_signed(t[i].speed_raw, lo, hi);
    f.push_back(lo);
    f.push_back(hi);
    f.push_back(static_cast<uint8_t>(t[i].resolution & 0xFF));
    f.push_back(static_cast<uint8_t>((t[i].resolution >> 8) & 0xFF));
  }
  for (int i = 0; i < 2; i++) f.push_back(ld2450_proto::DATA_TAIL[i]);
  return f;
}

int run() {
  Target out[3];
  uint8_t count = 0;

  // --- 1. Empty frame: no targets present ---
  {
    RawTarget raw[3] = {{0, 0, 0, 0, true}, {0, 0, 0, 0, true}, {0, 0, 0, 0, true}};
    auto f = build_frame(raw);
    CHECK(f.size() == ld2450_proto::FRAME_LEN);
    CHECK(ld2450_proto::parse_data_frame(f.data(), f.size(), out, count));
    CHECK(count == 0);
    CHECK(!out[0].active && !out[1].active && !out[2].active);
    CHECK(out[0].x == 0 && out[0].y == 0);
  }

  // --- 2. Single positive target ---
  {
    RawTarget raw[3] = {{1000, 1500, 20, 240, false},
                        {0, 0, 0, 0, true},
                        {0, 0, 0, 0, true}};
    auto f = build_frame(raw);
    CHECK(ld2450_proto::parse_data_frame(f.data(), f.size(), out, count));
    CHECK(count == 1);
    CHECK(out[0].active);
    CHECK(out[0].x == 1000);
    CHECK(out[0].y == 1500);
    CHECK(out[0].speed == 200);  // 20 cm/s * 10 = 200 mm/s
    CHECK(out[0].resolution == 240);
    // distance = sqrt(1000^2 + 1500^2) ~= 1802.7 mm
    CHECK(out[0].distance() > 1802.0f && out[0].distance() < 1803.0f);
    // angle = atan2(1000, 1500) ~= 33.69 deg
    CHECK(out[0].angle() > 33.6f && out[0].angle() < 33.8f);
    CHECK(out[0].moving());
    CHECK(!out[1].active && !out[2].active && !out[1].moving());
  }

  // --- 3. Negative coordinates and negative speed ---
  {
    RawTarget raw[3] = {{-500, -2000, -15, 100, false},
                        {0, 0, 0, 0, true},
                        {0, 0, 0, 0, true}};
    auto f = build_frame(raw);
    CHECK(ld2450_proto::parse_data_frame(f.data(), f.size(), out, count));
    CHECK(count == 1);
    CHECK(out[0].x == -500);
    CHECK(out[0].y == -2000);
    CHECK(out[0].speed == -150);
    CHECK(out[0].resolution == 100);
  }

  // --- 4. Three simultaneous targets ---
  {
    RawTarget raw[3] = {{100, 200, 5, 50, false},
                        {-300, 400, -10, 60, false},
                        {1234, -567, 30, 70, false}};
    auto f = build_frame(raw);
    CHECK(ld2450_proto::parse_data_frame(f.data(), f.size(), out, count));
    CHECK(count == 3);
    CHECK(out[0].x == 100 && out[0].y == 200 && out[0].speed == 50);
    CHECK(out[1].x == -300 && out[1].y == 400 && out[1].speed == -100);
    CHECK(out[2].x == 1234 && out[2].y == -567 && out[2].speed == 300);
  }

  // --- 5. Reject frames with a bad header ---
  {
    RawTarget raw[3] = {{100, 200, 5, 50, false}, {0, 0, 0, 0, true}, {0, 0, 0, 0, true}};
    auto f = build_frame(raw);
    f[0] = 0x00;  // corrupt header
    CHECK(!ld2450_proto::parse_data_frame(f.data(), f.size(), out, count));
  }

  // --- 6. Reject frames with a bad tail ---
  {
    RawTarget raw[3] = {{100, 200, 5, 50, false}, {0, 0, 0, 0, true}, {0, 0, 0, 0, true}};
    auto f = build_frame(raw);
    f[ld2450_proto::FRAME_LEN - 1] = 0x00;  // corrupt tail
    CHECK(!ld2450_proto::parse_data_frame(f.data(), f.size(), out, count));
  }

  // --- 7. Reject short buffers ---
  {
    std::vector<uint8_t> tooShort(ld2450_proto::FRAME_LEN - 1, 0xAA);
    CHECK(!ld2450_proto::parse_data_frame(tooShort.data(), tooShort.size(), out, count));
  }

  return 0;
}

int main() {
  int rc = run();
  if (rc == 0) {
    std::printf("OK: all %d checks passed\n", g_checks);
  }
  return rc;
}
