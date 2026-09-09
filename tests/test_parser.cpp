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
// The second half feeds command ACK frames into parse_ack_frame (MAC query =
// Bluetooth state, tracking-mode query, error status, resync cases).

#include "ld2450_protocol.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using ld2450_proto::Ack;
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

// Build a command ACK frame: FD FC FB FA | plen | cmd 01 | status | data | 04 03 02 01
static std::vector<uint8_t> build_ack(uint8_t cmd, uint16_t status, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> f;
  for (int i = 0; i < 4; i++) f.push_back(ld2450_proto::CMD_HEADER[i]);
  const uint16_t plen = static_cast<uint16_t>(4 + data.size());
  f.push_back(static_cast<uint8_t>(plen & 0xFF));
  f.push_back(static_cast<uint8_t>(plen >> 8));
  f.push_back(cmd);
  f.push_back(0x01);
  f.push_back(static_cast<uint8_t>(status & 0xFF));
  f.push_back(static_cast<uint8_t>(status >> 8));
  f.insert(f.end(), data.begin(), data.end());
  for (int i = 0; i < 4; i++) f.push_back(ld2450_proto::CMD_FOOTER[i]);
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

  // --- 8. ACK: MAC query reply with a real MAC => Bluetooth on ---
  {
    auto f = build_ack(ld2450_proto::CMD_QUERY_MAC, 0x0000, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66});
    CHECK(f.size() == 20);
    Ack ack;
    CHECK(ld2450_proto::parse_ack_frame(f.data(), f.size(), ack) == 20);
    CHECK(ack.command == ld2450_proto::CMD_QUERY_MAC);
    CHECK(ack.ok());
    CHECK(ack.data_len == 6);
    CHECK(ack.data[0] == 0x11 && ack.data[5] == 0x66);
    CHECK(ld2450_proto::mac_reply_means_bluetooth_on(ack));
    // Bytes following the frame (start of the next data frame) must not matter.
    f.push_back(0xAA);
    f.push_back(0xFF);
    CHECK(ld2450_proto::parse_ack_frame(f.data(), f.size(), ack) == 20);
  }

  // --- 9. ACK: MAC query reply with the NO_MAC sentinel => Bluetooth off ---
  {
    std::vector<uint8_t> no_mac(ld2450_proto::NO_MAC, ld2450_proto::NO_MAC + 6);
    auto f = build_ack(ld2450_proto::CMD_QUERY_MAC, 0x0000, no_mac);
    Ack ack;
    CHECK(ld2450_proto::parse_ack_frame(f.data(), f.size(), ack) == static_cast<int>(f.size()));
    CHECK(ack.ok());
    CHECK(!ld2450_proto::mac_reply_means_bluetooth_on(ack));
  }

  // --- 10. ACK: tracking-mode query, and a plain command ACK without data ---
  {
    auto f = build_ack(ld2450_proto::CMD_QUERY_TARGET_MODE, 0x0000, {0x02, 0x00});
    Ack ack;
    CHECK(ld2450_proto::parse_ack_frame(f.data(), f.size(), ack) == static_cast<int>(f.size()));
    CHECK(ack.command == ld2450_proto::CMD_QUERY_TARGET_MODE);
    CHECK(ack.data_len == 2 && ack.data[0] == 0x02);

    auto g = build_ack(ld2450_proto::CMD_ENABLE_CONFIG, 0x0000, {});
    CHECK(g.size() == 14);
    CHECK(ld2450_proto::parse_ack_frame(g.data(), g.size(), ack) == 14);
    CHECK(ack.command == ld2450_proto::CMD_ENABLE_CONFIG && ack.data_len == 0 && ack.ok());
  }

  // --- 11. ACK: failure status is reported, not treated as success ---
  {
    auto f = build_ack(ld2450_proto::CMD_BLUETOOTH, 0x0001, {});
    Ack ack;
    CHECK(ld2450_proto::parse_ack_frame(f.data(), f.size(), ack) > 0);
    CHECK(!ack.ok());
    CHECK(ack.status == 1);
  }

  // --- 12. ACK: incomplete frames ask for more bytes at every prefix length ---
  {
    auto f = build_ack(ld2450_proto::CMD_QUERY_MAC, 0x0000, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66});
    Ack ack;
    for (size_t n = 0; n < f.size(); n++) {
      CHECK(ld2450_proto::parse_ack_frame(f.data(), n, ack) == 0);
    }
  }

  // --- 13. ACK: corrupt frames are rejected (caller drops a byte and resyncs) ---
  {
    auto f = build_ack(ld2450_proto::CMD_QUERY_MAC, 0x0000, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66});
    Ack ack;
    auto bad_footer = f;
    bad_footer[bad_footer.size() - 1] = 0x00;
    CHECK(ld2450_proto::parse_ack_frame(bad_footer.data(), bad_footer.size(), ack) < 0);
    auto bad_marker = f;
    bad_marker[7] = 0x00;  // not an ACK (would be a command echo)
    CHECK(ld2450_proto::parse_ack_frame(bad_marker.data(), bad_marker.size(), ack) < 0);
    auto bad_header = f;
    bad_header[1] = 0x00;
    CHECK(ld2450_proto::parse_ack_frame(bad_header.data(), bad_header.size(), ack) < 0);
    // A bogus huge length must be rejected immediately instead of waiting forever.
    auto huge = f;
    huge[4] = 0xFF;
    huge[5] = 0x7F;
    CHECK(ld2450_proto::parse_ack_frame(huge.data(), huge.size(), ack) < 0);
    // A payload shorter than cmd+status is invalid too.
    auto tiny = f;
    tiny[4] = 0x02;
    CHECK(ld2450_proto::parse_ack_frame(tiny.data(), tiny.size(), ack) < 0);
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
