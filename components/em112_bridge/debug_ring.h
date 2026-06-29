#pragma once

#include <stddef.h>
#include <stdint.h>

namespace esphome {
namespace em112_bridge {

struct DebugRingEntry {
  uint32_t timestamp_ms{0};
  uint8_t slave_id{0};
  uint8_t function_code{0};
  uint16_t start_address{0};
  uint16_t quantity{0};
  bool crc_ok{false};
  uint8_t exception_code{0};
  uint16_t response_byte_count{0};
  char frame_hex[97]{};
  char note[24]{};
};

class DebugRing {
 public:
  static constexpr size_t CAPACITY = 20;

  void push(const DebugRingEntry &entry);
  size_t size() const { return count_; }
  bool get_newest(size_t index, DebugRingEntry *entry) const;
  const DebugRingEntry &latest() const { return entries_[(head_ + CAPACITY - 1) % CAPACITY]; }

 private:
  DebugRingEntry entries_[CAPACITY]{};
  size_t head_{0};
  size_t count_{0};
};

void bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t out_len);

}  // namespace em112_bridge
}  // namespace esphome
