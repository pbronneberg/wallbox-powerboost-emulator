#include "em112_registers.h"

#include <math.h>
#include <string.h>

namespace esphome {
namespace em112_bridge {

namespace {

float clampf(float value, float min_value, float max_value) {
  if (value < min_value)
    return min_value;
  if (value > max_value)
    return max_value;
  return value;
}

float opt_or(const OptionalFloat &value, float fallback) { return value.available ? value.value : fallback; }

bool opt_pair_net_kw(const OptionalFloat &delivered, const OptionalFloat &returned, float *net_kw) {
  if (!delivered.available || !returned.available)
    return false;
  *net_kw = delivered.value - returned.value;
  return true;
}

OptionalFloat selected_voltage(const DsmrActualValues &actual, SourcePhase phase) {
  switch (phase) {
    case SourcePhase::L2:
      return actual.voltage_l2;
    case SourcePhase::L3:
      return actual.voltage_l3;
    case SourcePhase::TOTAL:
    case SourcePhase::L1:
    default:
      return actual.voltage_l1;
  }
}

OptionalFloat selected_current(const DsmrActualValues &actual, SourcePhase phase) {
  switch (phase) {
    case SourcePhase::L2:
      return actual.current_l2;
    case SourcePhase::L3:
      return actual.current_l3;
    case SourcePhase::TOTAL:
    case SourcePhase::L1:
    default:
      return actual.current_l1;
  }
}

bool selected_phase_net_kw(const DsmrActualValues &actual, SourcePhase phase, float *net_kw) {
  switch (phase) {
    case SourcePhase::L1:
      return opt_pair_net_kw(actual.power_delivered_l1, actual.power_returned_l1, net_kw);
    case SourcePhase::L2:
      return opt_pair_net_kw(actual.power_delivered_l2, actual.power_returned_l2, net_kw);
    case SourcePhase::L3:
      return opt_pair_net_kw(actual.power_delivered_l3, actual.power_returned_l3, net_kw);
    case SourcePhase::TOTAL:
    default:
      return false;
  }
}

int32_t scale_s32(float value) {
  if (value >= 0.0f)
    return static_cast<int32_t>(value + 0.5f);
  return static_cast<int32_t>(value - 0.5f);
}

uint32_t scale_u32(float value) {
  if (value <= 0.0f)
    return 0;
  return static_cast<uint32_t>(value + 0.5f);
}

void copy_timestamp(char *dest, size_t len, const char *src) {
  if (len == 0)
    return;
  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }
  strncpy(dest, src, len - 1);
  dest[len - 1] = '\0';
}

}  // namespace

void Em112RegisterModel::update_from_dsmr(const DsmrActualValues &actual, const MeterRuntimeConfig &config,
                                          uint32_t now_ms, bool api_connected) {
  float total_net_kw = 0.0f;
  const bool total_available = opt_pair_net_kw(actual.power_delivered, actual.power_returned, &total_net_kw);

  float selected_net_kw = total_net_kw;
  float phase_net_kw = 0.0f;
  if (config.source_phase != SourcePhase::TOTAL && selected_phase_net_kw(actual, config.source_phase, &phase_net_kw)) {
    selected_net_kw = phase_net_kw;
  } else if (!total_available) {
    selected_net_kw = 0.0f;
  }

  snapshot_.grid_net_power_w = selected_net_kw * 1000.0f;
  snapshot_.grid_import_power_w = snapshot_.grid_net_power_w > 0.0f ? snapshot_.grid_net_power_w : 0.0f;
  snapshot_.grid_export_power_w = snapshot_.grid_net_power_w < 0.0f ? -snapshot_.grid_net_power_w : 0.0f;

  OptionalFloat voltage = selected_voltage(actual, config.source_phase);
  snapshot_.selected_voltage_v = opt_or(voltage, opt_or(actual.voltage_l1, 230.0f));
  snapshot_.selected_voltage_v = clampf(snapshot_.selected_voltage_v, 180.0f, 260.0f);

  const float calculated_current = fabsf(snapshot_.grid_net_power_w) / snapshot_.selected_voltage_v;
  const float dsmr_current = opt_or(selected_current(actual, config.source_phase), 0.0f);
  snapshot_.selected_current_a = clampf(dsmr_current > calculated_current ? dsmr_current : calculated_current, 0.0f,
                                        120.0f);

  const float import_t1 = opt_or(actual.energy_delivered_tariff1, 0.0f);
  const float import_t2 = opt_or(actual.energy_delivered_tariff2, 0.0f);
  const float export_t1 = opt_or(actual.energy_returned_tariff1, 0.0f);
  const float export_t2 = opt_or(actual.energy_returned_tariff2, 0.0f);
  snapshot_.import_energy_kwh = import_t1 + import_t2;
  snapshot_.export_energy_kwh = export_t1 + export_t2;

  snapshot_.apparent_power_va = snapshot_.selected_voltage_v * snapshot_.selected_current_a;
  snapshot_.reactive_power_var = 0.0f;
  snapshot_.power_factor = 1.0f;
  snapshot_.frequency_hz = 50.0f;
  snapshot_.api_connected = api_connected;
  snapshot_.data_stale = false;
  snapshot_.fail_safe_active = false;
  snapshot_.last_success_ms = now_ms;
  copy_timestamp(snapshot_.timestamp, sizeof(snapshot_.timestamp), actual.timestamp);
}

void Em112RegisterModel::set_stale(uint32_t now_ms, const MeterRuntimeConfig &config) {
  snapshot_.grid_net_power_w = config.fail_safe_import_power_w;
  snapshot_.grid_import_power_w = config.fail_safe_import_power_w;
  snapshot_.grid_export_power_w = 0.0f;
  snapshot_.selected_voltage_v = clampf(snapshot_.selected_voltage_v > 0.0f ? snapshot_.selected_voltage_v : 230.0f,
                                        180.0f, 260.0f);
  snapshot_.selected_current_a = clampf(config.fail_safe_import_power_w / snapshot_.selected_voltage_v, 0.0f, 120.0f);
  snapshot_.apparent_power_va = snapshot_.selected_voltage_v * snapshot_.selected_current_a;
  snapshot_.reactive_power_var = 0.0f;
  snapshot_.power_factor = 1.0f;
  snapshot_.frequency_hz = 50.0f;
  snapshot_.api_connected = false;
  snapshot_.data_stale = true;
  snapshot_.fail_safe_active = true;
  (void) now_ms;
}

bool Em112RegisterModel::read_register(uint16_t address, uint16_t *value, bool strict) const {
  uint16_t low = 0;
  uint16_t high = 0;

  switch (address) {
    case 0x0000:
    case 0x0001:
      pack_s32_lsw_first(scale_s32(snapshot_.selected_voltage_v * 10.0f), &low, &high);
      *value = address == 0x0000 ? low : high;
      return true;
    case 0x0002:
    case 0x0003:
      pack_s32_lsw_first(scale_s32(snapshot_.selected_current_a * 1000.0f), &low, &high);
      *value = address == 0x0002 ? low : high;
      return true;
    case 0x0004:
    case 0x0005:
      pack_s32_lsw_first(scale_s32(snapshot_.grid_net_power_w * 10.0f), &low, &high);
      *value = address == 0x0004 ? low : high;
      return true;
    case 0x0006:
    case 0x0007:
      pack_s32_lsw_first(scale_s32(snapshot_.apparent_power_va * 10.0f), &low, &high);
      *value = address == 0x0006 ? low : high;
      return true;
    case 0x0008:
    case 0x0009:
      pack_s32_lsw_first(scale_s32(snapshot_.reactive_power_var * 10.0f), &low, &high);
      *value = address == 0x0008 ? low : high;
      return true;
    case 0x000B:
      *value = PRODUCT_ID;
      return true;
    case 0x000E:
      *value = static_cast<uint16_t>(scale_s32(snapshot_.power_factor * 1000.0f));
      return true;
    case 0x000F:
      *value = static_cast<uint16_t>(scale_s32(snapshot_.frequency_hz * 10.0f));
      return true;
    case 0x0010:
    case 0x0011:
      pack_u32_lsw_first(scale_u32(snapshot_.import_energy_kwh * 10.0f), &low, &high);
      *value = address == 0x0010 ? low : high;
      return true;
    case 0x0020:
    case 0x0021:
      pack_u32_lsw_first(scale_u32(snapshot_.export_energy_kwh * 10.0f), &low, &high);
      *value = address == 0x0020 ? low : high;
      return true;
    default:
      *value = 0;
      return !strict;
  }
}

bool Em112RegisterModel::read_registers(uint16_t start_address, uint16_t quantity, uint16_t *values,
                                        size_t value_capacity, bool strict) const {
  if (quantity > value_capacity)
    return false;
  for (uint16_t i = 0; i < quantity; i++) {
    if (!read_register(start_address + i, &values[i], strict))
      return false;
  }
  return true;
}

void Em112RegisterModel::pack_s32_lsw_first(int32_t scaled, uint16_t *low_word, uint16_t *high_word) {
  const uint32_t raw = static_cast<uint32_t>(scaled);
  *low_word = static_cast<uint16_t>(raw & 0xFFFFu);
  *high_word = static_cast<uint16_t>((raw >> 16) & 0xFFFFu);
}

void Em112RegisterModel::pack_u32_lsw_first(uint32_t scaled, uint16_t *low_word, uint16_t *high_word) {
  *low_word = static_cast<uint16_t>(scaled & 0xFFFFu);
  *high_word = static_cast<uint16_t>((scaled >> 16) & 0xFFFFu);
}

const char *source_phase_to_string(SourcePhase phase) {
  switch (phase) {
    case SourcePhase::TOTAL:
      return "total";
    case SourcePhase::L2:
      return "l2";
    case SourcePhase::L3:
      return "l3";
    case SourcePhase::L1:
    default:
      return "l1";
  }
}

SourcePhase source_phase_from_string(const char *value, SourcePhase fallback) {
  if (value == nullptr)
    return fallback;
  if (strcmp(value, "total") == 0)
    return SourcePhase::TOTAL;
  if (strcmp(value, "l1") == 0)
    return SourcePhase::L1;
  if (strcmp(value, "l2") == 0)
    return SourcePhase::L2;
  if (strcmp(value, "l3") == 0)
    return SourcePhase::L3;
  return fallback;
}

}  // namespace em112_bridge
}  // namespace esphome
