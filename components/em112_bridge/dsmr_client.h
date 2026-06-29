#pragma once

#include "em112_registers.h"

#include <stddef.h>
#include <stdint.h>

namespace esphome {
namespace em112_bridge {

bool parse_dsmr_actual_json(const char *json, DsmrActualValues *out, char *error, size_t error_len);
bool parse_numeric_value(const char *value, float *out);

}  // namespace em112_bridge
}  // namespace esphome
