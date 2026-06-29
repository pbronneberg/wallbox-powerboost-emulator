#include "debug_ring.h"

#include <string.h>

namespace esphome {
namespace em112_bridge {

void DebugRing::push(const DebugRingEntry &entry) {
  entries_[head_] = entry;
  head_ = (head_ + 1) % CAPACITY;
  if (count_ < CAPACITY)
    count_++;
}

bool DebugRing::get_newest(size_t index, DebugRingEntry *entry) const {
  if (entry == nullptr || index >= count_)
    return false;
  const size_t pos = (head_ + CAPACITY - 1 - index) % CAPACITY;
  *entry = entries_[pos];
  return true;
}

void bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t out_len) {
  static constexpr char HEX[] = "0123456789ABCDEF";
  if (out == nullptr || out_len == 0)
    return;
  size_t used = 0;
  for (size_t i = 0; i < len && used + 2 < out_len; i++) {
    out[used++] = HEX[(bytes[i] >> 4) & 0x0F];
    out[used++] = HEX[bytes[i] & 0x0F];
  }
  out[used] = '\0';
}

}  // namespace em112_bridge
}  // namespace esphome
