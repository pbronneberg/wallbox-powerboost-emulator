#pragma once

#include "debug_ring.h"
#include "em112_registers.h"

#include <stddef.h>
#include <stdint.h>

namespace esphome {
namespace em112_bridge {

struct ModbusCounters {
  uint32_t total_requests{0};
  uint32_t crc_errors{0};
  uint32_t unsupported_functions{0};
  uint32_t illegal_addresses{0};
  uint32_t exceptions{0};
  uint32_t wrong_slave_ids{0};
  uint8_t last_function_code{0};
  uint16_t last_start_address{0};
  uint16_t last_quantity{0};
  uint8_t last_exception_code{0};
  bool last_crc_ok{false};
  uint32_t last_wallbox_poll_ms{0};
};

struct ModbusSettings {
  uint8_t slave_id{1};
  bool strict_exceptions{false};
  bool ack_writes_without_change{false};
};

class ModbusRtuSlave {
 public:
  static constexpr size_t MAX_FRAME = 256;

  static uint16_t crc16(const uint8_t *data, size_t len);
  static bool validate_crc(const uint8_t *frame, size_t len);

  size_t process_frame(const uint8_t *request, size_t request_len, uint8_t *response, size_t response_capacity,
                       const Em112RegisterModel &registers, const ModbusSettings &settings, uint32_t now_ms,
                       DebugRingEntry *debug_entry);

  const ModbusCounters &counters() const { return counters_; }

 private:
  ModbusCounters counters_{};

  size_t exception_response(uint8_t slave_id, uint8_t function_code, uint8_t exception_code, uint8_t *response,
                            size_t capacity);
};

}  // namespace em112_bridge
}  // namespace esphome
