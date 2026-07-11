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

enum class MeterProfile : uint8_t {
  EM112_PFB = 0,
  EM330_AV5 = 1,
  EM530_AV5 = 2,
};

struct OptionalFloat {
  bool available{false};
  float value{0.0f};
};

struct OptionalDouble {
  bool available{false};
  double value{0.0};
};

struct DsmrActualValues {
  char timestamp[32]{};
  OptionalDouble energy_delivered_tariff1;
  OptionalDouble energy_delivered_tariff2;
  OptionalDouble energy_returned_tariff1;
  OptionalDouble energy_returned_tariff2;
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
  double import_energy_kwh{0.0};
  double export_energy_kwh{0.0};
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
  static constexpr uint16_t EM330_PRODUCT_ID = 332;
  static constexpr uint16_t EM530_PRODUCT_ID = 1744;

  void set_profile(MeterProfile profile) { profile_ = profile; }
  MeterProfile profile() const { return profile_; }
  void update_from_dsmr(const DsmrActualValues &actual, const MeterRuntimeConfig &config, uint32_t now_ms,
                        bool api_connected);
  void set_stale(uint32_t now_ms, const MeterRuntimeConfig &config);
  const MeterSnapshot &snapshot() const { return snapshot_; }

  bool read_register(uint16_t address, uint16_t *value, bool strict) const;
  bool read_registers(uint16_t start_address, uint16_t quantity, uint16_t *values, size_t value_capacity,
                      bool strict) const;

  static void pack_s32_lsw_first(int32_t scaled, uint16_t *low_word, uint16_t *high_word);
  static void pack_u32_lsw_first(uint32_t scaled, uint16_t *low_word, uint16_t *high_word);
  static void pack_u64_lsw_first(uint64_t scaled, uint16_t *words);

 private:
  bool read_em112_register_(uint16_t address, uint16_t *value, bool strict) const;
  bool read_em3xx_register_(uint16_t address, uint16_t *value, bool strict) const;
  MeterSnapshot snapshot_{};
  MeterProfile profile_{MeterProfile::EM112_PFB};
};

const char *source_phase_to_string(SourcePhase phase);
SourcePhase source_phase_from_string(const char *value, SourcePhase fallback = SourcePhase::L1);
const char *meter_profile_to_string(MeterProfile profile);
const char *meter_profile_display_name(MeterProfile profile);
MeterProfile meter_profile_from_string(const char *value, MeterProfile fallback = MeterProfile::EM112_PFB);

}  // namespace em112_bridge
}  // namespace esphome
