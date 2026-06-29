#pragma once

#include <stdint.h>
#include <stddef.h>

namespace esphome {
namespace em112_bridge {

enum class SourcePhase : uint8_t {
  TOTAL = 0,
  L1 = 1,
  L2 = 2,
  L3 = 3,
};

struct OptionalFloat {
  bool available{false};
  float value{0.0f};
};

struct DsmrActualValues {
  char timestamp[32]{};
  OptionalFloat energy_delivered_tariff1;
  OptionalFloat energy_delivered_tariff2;
  OptionalFloat energy_returned_tariff1;
  OptionalFloat energy_returned_tariff2;
  OptionalFloat power_delivered;
  OptionalFloat power_returned;
  OptionalFloat voltage_l1;
  OptionalFloat voltage_l2;
  OptionalFloat voltage_l3;
  OptionalFloat current_l1;
  OptionalFloat current_l2;
  OptionalFloat current_l3;
  OptionalFloat power_delivered_l1;
  OptionalFloat power_delivered_l2;
  OptionalFloat power_delivered_l3;
  OptionalFloat power_returned_l1;
  OptionalFloat power_returned_l2;
  OptionalFloat power_returned_l3;
};

struct MeterRuntimeConfig {
  SourcePhase source_phase{SourcePhase::L1};
  float fail_safe_import_power_w{7000.0f};
  uint32_t stale_timeout_s{30};
};

struct MeterSnapshot {
  float grid_net_power_w{7000.0f};
  float grid_import_power_w{7000.0f};
  float grid_export_power_w{0.0f};
  float selected_voltage_v{230.0f};
  float selected_current_a{30.435f};
  float import_energy_kwh{0.0f};
  float export_energy_kwh{0.0f};
  float apparent_power_va{7000.0f};
  float reactive_power_var{0.0f};
  float power_factor{1.0f};
  float frequency_hz{50.0f};
  bool api_connected{false};
  bool data_stale{true};
  bool fail_safe_active{true};
  char timestamp[32]{};
  uint32_t last_success_ms{0};
};

class Em112RegisterModel {
 public:
  static constexpr uint16_t PRODUCT_ID = 104;

  void update_from_dsmr(const DsmrActualValues &actual, const MeterRuntimeConfig &config, uint32_t now_ms,
                        bool api_connected);
  void set_stale(uint32_t now_ms, const MeterRuntimeConfig &config);
  const MeterSnapshot &snapshot() const { return snapshot_; }

  bool read_register(uint16_t address, uint16_t *value, bool strict) const;
  bool read_registers(uint16_t start_address, uint16_t quantity, uint16_t *values, size_t value_capacity,
                      bool strict) const;

  static void pack_s32_lsw_first(int32_t scaled, uint16_t *low_word, uint16_t *high_word);
  static void pack_u32_lsw_first(uint32_t scaled, uint16_t *low_word, uint16_t *high_word);

 private:
  MeterSnapshot snapshot_{};
};

const char *source_phase_to_string(SourcePhase phase);
SourcePhase source_phase_from_string(const char *value, SourcePhase fallback = SourcePhase::L1);

}  // namespace em112_bridge
}  // namespace esphome
