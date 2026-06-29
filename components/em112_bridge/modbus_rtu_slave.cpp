#include "modbus_rtu_slave.h"

#include <string.h>

namespace esphome {
namespace em112_bridge {

namespace {

uint16_t be16(const uint8_t *p) { return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]); }

void append_crc(uint8_t *frame, size_t len_without_crc) {
  const uint16_t crc = ModbusRtuSlave::crc16(frame, len_without_crc);
  frame[len_without_crc] = static_cast<uint8_t>(crc & 0xFF);
  frame[len_without_crc + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
}

void note(DebugRingEntry *entry, const char *text) {
  if (entry == nullptr)
    return;
  strncpy(entry->note, text, sizeof(entry->note) - 1);
  entry->note[sizeof(entry->note) - 1] = '\0';
}

}  // namespace

uint16_t ModbusRtuSlave::crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t pos = 0; pos < len; pos++) {
    crc ^= static_cast<uint16_t>(data[pos]);
    for (uint8_t i = 0; i < 8; i++) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

bool ModbusRtuSlave::validate_crc(const uint8_t *frame, size_t len) {
  if (len < 4)
    return false;
  const uint16_t expected = crc16(frame, len - 2);
  const uint16_t actual = static_cast<uint16_t>(frame[len - 2] | (frame[len - 1] << 8));
  return expected == actual;
}

size_t ModbusRtuSlave::exception_response(uint8_t slave_id, uint8_t function_code, uint8_t exception_code,
                                          uint8_t *response, size_t capacity) {
  if (capacity < 5)
    return 0;
  response[0] = slave_id;
  response[1] = function_code | 0x80;
  response[2] = exception_code;
  append_crc(response, 3);
  counters_.exceptions++;
  counters_.last_exception_code = exception_code;
  return 5;
}

size_t ModbusRtuSlave::process_frame(const uint8_t *request, size_t request_len, uint8_t *response,
                                     size_t response_capacity, const Em112RegisterModel &registers,
                                     const ModbusSettings &settings, uint32_t now_ms, DebugRingEntry *debug_entry) {
  if (debug_entry != nullptr) {
    *debug_entry = DebugRingEntry{};
    debug_entry->timestamp_ms = now_ms;
    if (request_len > 0)
      debug_entry->slave_id = request[0];
    if (request_len > 1)
      debug_entry->function_code = request[1];
    bytes_to_hex(request, request_len, debug_entry->frame_hex, sizeof(debug_entry->frame_hex));
  }

  if (request_len < 4) {
    note(debug_entry, "short_frame");
    return 0;
  }

  const uint8_t slave_id = request[0];
  const uint8_t function_code = request[1];
  const bool crc_ok = validate_crc(request, request_len);
  counters_.last_crc_ok = crc_ok;
  if (debug_entry != nullptr)
    debug_entry->crc_ok = crc_ok;

  if (!crc_ok) {
    counters_.crc_errors++;
    note(debug_entry, "bad_crc");
    return 0;
  }

  if (slave_id == 0) {
    note(debug_entry, "broadcast");
    return 0;
  }

  if (slave_id != settings.slave_id) {
    counters_.wrong_slave_ids++;
    note(debug_entry, "wrong_slave");
    return 0;
  }

  counters_.total_requests++;
  counters_.last_function_code = function_code;
  counters_.last_wallbox_poll_ms = now_ms;

  if (function_code == 0x03 || function_code == 0x04) {
    if (request_len < 8) {
      const size_t size = exception_response(slave_id, function_code, 0x03, response, response_capacity);
      if (debug_entry != nullptr) {
        debug_entry->exception_code = 0x03;
        debug_entry->response_byte_count = size;
      }
      note(debug_entry, "bad_length");
      return size;
    }
    const uint16_t start_address = be16(&request[2]);
    const uint16_t quantity = be16(&request[4]);
    counters_.last_start_address = start_address;
    counters_.last_quantity = quantity;
    if (debug_entry != nullptr) {
      debug_entry->start_address = start_address;
      debug_entry->quantity = quantity;
    }

    if (quantity == 0 || quantity > 125 || response_capacity < static_cast<size_t>(5 + quantity * 2)) {
      const size_t size = exception_response(slave_id, function_code, 0x03, response, response_capacity);
      if (debug_entry != nullptr) {
        debug_entry->exception_code = 0x03;
        debug_entry->response_byte_count = size;
      }
      note(debug_entry, "bad_quantity");
      return size;
    }

    uint16_t values[125]{};
    if (!registers.read_registers(start_address, quantity, values, 125, settings.strict_exceptions)) {
      counters_.illegal_addresses++;
      const size_t size = exception_response(slave_id, function_code, 0x02, response, response_capacity);
      if (debug_entry != nullptr) {
        debug_entry->exception_code = 0x02;
        debug_entry->response_byte_count = size;
      }
      note(debug_entry, "unknown_register");
      return size;
    }

    response[0] = slave_id;
    response[1] = function_code;
    response[2] = static_cast<uint8_t>(quantity * 2);
    for (uint16_t i = 0; i < quantity; i++) {
      response[3 + (i * 2)] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
      response[4 + (i * 2)] = static_cast<uint8_t>(values[i] & 0xFF);
    }
    const size_t response_len = 3 + (quantity * 2) + 2;
    append_crc(response, response_len - 2);
    if (debug_entry != nullptr)
      debug_entry->response_byte_count = response_len;
    note(debug_entry, "ok");
    return response_len;
  }

  if (function_code == 0x08) {
    if (request_len < 8) {
      const size_t size = exception_response(slave_id, function_code, 0x03, response, response_capacity);
      if (debug_entry != nullptr) {
        debug_entry->exception_code = 0x03;
        debug_entry->response_byte_count = size;
      }
      note(debug_entry, "bad_length");
      return size;
    }
    const uint16_t subfunction = be16(&request[2]);
    counters_.last_start_address = subfunction;
    counters_.last_quantity = 0;
    if (subfunction == 0x0000 && response_capacity >= request_len) {
      memcpy(response, request, request_len);
      if (debug_entry != nullptr)
        debug_entry->response_byte_count = request_len;
      note(debug_entry, "ok");
      return request_len;
    }
    counters_.unsupported_functions++;
    const size_t size = exception_response(slave_id, function_code, 0x01, response, response_capacity);
    if (debug_entry != nullptr) {
      debug_entry->exception_code = 0x01;
      debug_entry->response_byte_count = size;
    }
    note(debug_entry, "unsupported_fc08");
    return size;
  }

  if (function_code == 0x06) {
    if (debug_entry != nullptr && request_len >= 8) {
      debug_entry->start_address = be16(&request[2]);
      debug_entry->quantity = 1;
    }
    if (settings.ack_writes_without_change && response_capacity >= request_len) {
      memcpy(response, request, request_len);
      if (debug_entry != nullptr)
        debug_entry->response_byte_count = request_len;
      note(debug_entry, "write_acked_no_change");
      return request_len;
    }
    const size_t size = exception_response(slave_id, function_code, 0x01, response, response_capacity);
    if (debug_entry != nullptr) {
      debug_entry->exception_code = 0x01;
      debug_entry->response_byte_count = size;
    }
    note(debug_entry, "write_rejected");
    return size;
  }

  counters_.unsupported_functions++;
  const size_t size = exception_response(slave_id, function_code, 0x01, response, response_capacity);
  if (debug_entry != nullptr) {
    debug_entry->exception_code = 0x01;
    debug_entry->response_byte_count = size;
  }
  note(debug_entry, "unsupported_fc");
  return size;
}

}  // namespace em112_bridge
}  // namespace esphome
