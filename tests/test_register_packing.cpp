#include "dsmr_client.h"
#include "em112_registers.h"
#include "modbus_rtu_slave.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace esphome::em112_bridge;

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

void require_close(float actual, float expected, float tolerance, const char *message) {
  if (std::fabs(actual - expected) > tolerance) {
    std::fprintf(stderr, "FAIL: %s got %.4f expected %.4f\n", message, actual, expected);
    std::exit(1);
  }
}

void append_crc(uint8_t *frame, size_t len_without_crc) {
  const uint16_t crc = ModbusRtuSlave::crc16(frame, len_without_crc);
  frame[len_without_crc] = static_cast<uint8_t>(crc & 0xFF);
  frame[len_without_crc + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
}

DsmrActualValues sample_actual() {
  const char *json =
      "{\"actual\":["
      "{\"name\":\"timestamp\",\"value\":\"260629120000S\"},"
      "{\"name\":\"energy_delivered_tariff1\",\"value\":1000.1,\"unit\":\"kWh\"},"
      "{\"name\":\"energy_delivered_tariff2\",\"value\":\"2345.5\",\"unit\":\"kWh\"},"
      "{\"name\":\"energy_returned_tariff1\",\"value\":100.0,\"unit\":\"kWh\"},"
      "{\"name\":\"energy_returned_tariff2\",\"value\":\"23.4\",\"unit\":\"kWh\"},"
      "{\"name\":\"power_delivered\",\"value\":1.500,\"unit\":\"kW\"},"
      "{\"name\":\"power_returned\",\"value\":0.050,\"unit\":\"kW\"},"
      "{\"name\":\"voltage_l1\",\"value\":230.6,\"unit\":\"V\"},"
      "{\"name\":\"current_l1\",\"value\":6.000,\"unit\":\"A\"},"
      "{\"name\":\"power_delivered_l1\",\"value\":1.500,\"unit\":\"kW\"},"
      "{\"name\":\"power_returned_l1\",\"value\":0.050,\"unit\":\"kW\"}"
      "]}";
  DsmrActualValues actual{};
  char error[96]{};
  require(parse_dsmr_actual_json(json, &actual, error, sizeof(error)), error);
  return actual;
}

void test_dsmr_parse() {
  DsmrActualValues actual = sample_actual();
  require(std::strcmp(actual.timestamp, "260629120000S") == 0, "timestamp parsed");
  require_close(actual.energy_delivered_tariff1.value, 1000.1f, 0.001f, "energy tariff 1");
  require_close(actual.power_delivered.value, 1.5f, 0.001f, "power delivered");
  require(actual.voltage_l1.available, "voltage l1 available");
}

void test_missing_values() {
  const char *json = "{\"actual\":[{\"name\":\"voltage_l1\",\"value\":\"-\"},{\"name\":\"power_delivered\",\"value\":null}]}";
  DsmrActualValues actual{};
  char error[96]{};
  require(parse_dsmr_actual_json(json, &actual, error, sizeof(error)), "missing values parse");
  require(!actual.voltage_l1.available, "dash unavailable");
  require(!actual.power_delivered.available, "null unavailable");
}

void test_net_power_and_phase() {
  DsmrActualValues actual = sample_actual();
  MeterRuntimeConfig config{};
  config.source_phase = SourcePhase::L1;
  Em112RegisterModel model;
  model.update_from_dsmr(actual, config, 1000, true);
  require_close(model.snapshot().grid_net_power_w, 1450.0f, 0.1f, "positive import");

  actual.power_delivered.value = 0.100f;
  actual.power_returned.value = 1.000f;
  actual.power_delivered_l1.available = false;
  actual.power_returned_l1.available = false;
  model.update_from_dsmr(actual, config, 2000, true);
  require_close(model.snapshot().grid_net_power_w, -900.0f, 0.1f, "negative export fallback");

  config.source_phase = SourcePhase::TOTAL;
  model.update_from_dsmr(actual, config, 3000, true);
  require_close(model.snapshot().grid_net_power_w, -900.0f, 0.1f, "total mode");
}

void test_scaling_and_packing() {
  DsmrActualValues actual = sample_actual();
  MeterRuntimeConfig config{};
  config.source_phase = SourcePhase::L1;
  Em112RegisterModel model;
  model.update_from_dsmr(actual, config, 1000, true);

  uint16_t value = 0;
  require(model.read_register(0x0000, &value, true), "voltage low word");
  require(value == 2306, "230.6 V -> 2306");
  require(model.read_register(0x0002, &value, true), "current low word");
  require(value == 6288, "conservative current uses calculated value");
  require(model.read_register(0x0004, &value, true), "active power low word");
  require(value == 14500, "1450 W -> 14500");
  require(model.read_register(0x000B, &value, true), "product id");
  require(value == 104, "product ID 104");
  require(model.read_register(0x0010, &value, true), "import energy");
  require(value == 33456, "import energy kWh x10");

  uint16_t low = 0;
  uint16_t high = 0;
  Em112RegisterModel::pack_s32_lsw_first(-14500, &low, &high);
  require(low == 0xC75C && high == 0xFFFF, "negative s32 LSW/MSW packing");
}

void test_crc_and_fc03() {
  DsmrActualValues actual = sample_actual();
  MeterRuntimeConfig config{};
  Em112RegisterModel model;
  model.update_from_dsmr(actual, config, 1000, true);

  ModbusRtuSlave slave;
  ModbusSettings settings{};
  settings.slave_id = 1;
  uint8_t request[] = {1, 3, 0, 0, 0, 2, 0, 0};
  append_crc(request, 6);
  require(ModbusRtuSlave::validate_crc(request, sizeof(request)), "valid crc");

  uint8_t response[64]{};
  DebugRingEntry entry{};
  size_t response_len = slave.process_frame(request, sizeof(request), response, sizeof(response), model, settings, 5000, &entry);
  require(response_len == 9, "FC03 response length");
  require(response[0] == 1 && response[1] == 3 && response[2] == 4, "FC03 response header");
  require(response[3] == 0x09 && response[4] == 0x02, "register byte order high then low");
  require(entry.crc_ok && std::strcmp(entry.note, "ok") == 0, "debug entry ok");

  uint8_t fc04[] = {1, 4, 0, 0, 0, 2, 0, 0};
  append_crc(fc04, 6);
  response_len = slave.process_frame(fc04, sizeof(fc04), response, sizeof(response), model, settings, 6000, &entry);
  require(response_len == 9 && response[1] == 4, "FC04 response");
}

void test_unknown_and_stale() {
  Em112RegisterModel model;
  uint16_t value = 123;
  require(model.read_register(0x1234, &value, false), "courtesy unknown register");
  require(value == 0, "courtesy unknown returns zero");
  require(!model.read_register(0x1234, &value, true), "strict unknown register rejected");

  MeterRuntimeConfig config{};
  config.fail_safe_import_power_w = 7000;
  model.set_stale(10000, config);
  require_close(model.snapshot().grid_net_power_w, 7000.0f, 0.1f, "stale reports fail-safe import");
  require(model.snapshot().fail_safe_active, "fail-safe active");
}

// Real-world snapshot captured from dsmr-api.local on 2026-07-05:
// power_delivered=0.000 kW, power_returned=0.123 kW (solar export),
// current_l1=3 A (apparent/reactive current present despite low real power).
// mbus1..4 and gas_delivered fields are silently ignored.
void test_realworld_export_snapshot() {
  const char *json =
      "{\"actual\":["
      "{\"name\":\"timestamp\",\"value\":\"260705102353S\"},"
      "{\"name\":\"energy_delivered_tariff1\",\"value\":16164.660,\"unit\":\"kWh\"},"
      "{\"name\":\"energy_delivered_tariff2\",\"value\":11121.743,\"unit\":\"kWh\"},"
      "{\"name\":\"energy_returned_tariff1\",\"value\":6298.306,\"unit\":\"kWh\"},"
      "{\"name\":\"energy_returned_tariff2\",\"value\":15964.774,\"unit\":\"kWh\"},"
      "{\"name\":\"power_delivered\",\"value\":0.000,\"unit\":\"kW\"},"
      "{\"name\":\"power_returned\",\"value\":0.123,\"unit\":\"kW\"},"
      "{\"name\":\"voltage_l1\",\"value\":230.000,\"unit\":\"V\"},"
      "{\"name\":\"current_l1\",\"value\":3.000,\"unit\":\"A\"},"
      "{\"name\":\"power_delivered_l1\",\"value\":0.000,\"unit\":\"kW\"},"
      "{\"name\":\"power_returned_l1\",\"value\":0.123,\"unit\":\"kW\"},"
      "{\"name\":\"mbus1_delivered\",\"value\":10729.790,\"unit\":\"m3\"},"
      "{\"name\":\"mbus2_delivered\",\"value\":0.000,\"unit\":\"GJ\"},"
      "{\"name\":\"mbus3_delivered\",\"value\":0.000,\"unit\":\"m3\"},"
      "{\"name\":\"mbus4_delivered\",\"value\":0.000,\"unit\":\"m3\"},"
      "{\"name\":\"gas_delivered\",\"value\":10729.789,\"unit\":\"m3\"}"
      "]}";

  DsmrActualValues actual{};
  char error[96]{};
  require(parse_dsmr_actual_json(json, &actual, error, sizeof(error)), error);

  require(std::strcmp(actual.timestamp, "260705102353S") == 0, "realworld timestamp");
  require(actual.power_delivered.available, "power_delivered available (zero is valid)");
  require_close(actual.power_delivered.value, 0.0f, 0.001f, "power_delivered zero");
  require_close(actual.power_returned.value, 0.123f, 0.001f, "power_returned 0.123 kW");
  require_close(actual.power_returned_l1.value, 0.123f, 0.001f, "power_returned_l1 0.123 kW");
  require_close(actual.current_l1.value, 3.0f, 0.001f, "current_l1 3 A");
  require_close(actual.voltage_l1.value, 230.0f, 0.1f, "voltage_l1 230 V");
  require_close(actual.energy_delivered_tariff1.value, 16164.660f, 0.01f, "energy_delivered_tariff1");
  require_close(actual.energy_returned_tariff2.value, 15964.774f, 0.01f, "energy_returned_tariff2");

  // Register model: L1 phase selected, net power is export (-123 W).
  MeterRuntimeConfig config{};
  config.source_phase = SourcePhase::L1;
  Em112RegisterModel model;
  model.update_from_dsmr(actual, config, 5000, true);

  require_close(model.snapshot().grid_net_power_w, -123.0f, 0.5f, "realworld net power export");
  require_close(model.snapshot().grid_export_power_w, 123.0f, 0.5f, "realworld export power");
  require_close(model.snapshot().grid_import_power_w, 0.0f, 0.1f, "realworld import power zero");

  // Emulator uses DSMR current_l1 (3 A apparent) because it exceeds calculated (0.53 A).
  require_close(model.snapshot().selected_current_a, 3.0f, 0.01f, "realworld apparent current wins");

  // Total energy accumulators.
  require_close(model.snapshot().import_energy_kwh, 16164.660f + 11121.743f, 0.1f, "realworld import energy total");
  require_close(model.snapshot().export_energy_kwh, 6298.306f + 15964.774f, 0.1f, "realworld export energy total");
}

}  // namespace

int main() {
  test_dsmr_parse();
  test_missing_values();
  test_net_power_and_phase();
  test_scaling_and_packing();
  test_crc_and_fc03();
  test_unknown_and_stale();
  test_realworld_export_snapshot();
  std::puts("All host tests passed");
  return 0;
}
