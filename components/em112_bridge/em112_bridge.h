#pragma once

#include "debug_ring.h"
#include "dsmr_client.h"
#include "em112_registers.h"
#include "modbus_rtu_slave.h"

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#ifdef USE_WEBSERVER
#include "esphome/components/web_server_base/web_server_base.h"
#endif

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace esphome {
namespace em112_bridge {

class Em112Bridge : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_dsmr_url(const char *url);
  void set_poll_interval(uint32_t ms) { poll_interval_ms_ = ms; }
  void set_poll_interval_seconds(uint32_t seconds) { poll_interval_ms_ = seconds * 1000U; }
  void set_stale_timeout(uint32_t ms) { runtime_config_.stale_timeout_s = ms / 1000U; }
  void set_stale_timeout_seconds(uint32_t seconds) { runtime_config_.stale_timeout_s = seconds; }
  void set_fail_safe_import_power(float watts) { runtime_config_.fail_safe_import_power_w = watts; }
  void set_source_phase_string(const char *phase);
  void set_modbus_slave_id(uint8_t slave_id) { modbus_settings_.slave_id = slave_id; }
  void set_strict_exceptions(bool strict) { modbus_settings_.strict_exceptions = strict; }
  void set_debug_logging(bool enabled) { debug_logging_ = enabled; }

  void set_grid_net_power_sensor(sensor::Sensor *sensor) { grid_net_power_sensor_ = sensor; }
  void set_grid_import_power_sensor(sensor::Sensor *sensor) { grid_import_power_sensor_ = sensor; }
  void set_grid_export_power_sensor(sensor::Sensor *sensor) { grid_export_power_sensor_ = sensor; }
  void set_selected_voltage_sensor(sensor::Sensor *sensor) { selected_voltage_sensor_ = sensor; }
  void set_selected_current_sensor(sensor::Sensor *sensor) { selected_current_sensor_ = sensor; }
  void set_import_energy_sensor(sensor::Sensor *sensor) { import_energy_sensor_ = sensor; }
  void set_export_energy_sensor(sensor::Sensor *sensor) { export_energy_sensor_ = sensor; }
  void set_apparent_power_sensor(sensor::Sensor *sensor) { apparent_power_sensor_ = sensor; }
  void set_reactive_power_sensor(sensor::Sensor *sensor) { reactive_power_sensor_ = sensor; }
  void set_power_factor_sensor(sensor::Sensor *sensor) { power_factor_sensor_ = sensor; }
  void set_frequency_sensor(sensor::Sensor *sensor) { frequency_sensor_ = sensor; }
  void set_dsmr_last_poll_age_sensor(sensor::Sensor *sensor) { dsmr_last_poll_age_sensor_ = sensor; }
  void set_dsmr_response_time_sensor(sensor::Sensor *sensor) { dsmr_response_time_sensor_ = sensor; }
  void set_dsmr_successful_polls_sensor(sensor::Sensor *sensor) { dsmr_successful_polls_sensor_ = sensor; }
  void set_dsmr_failed_polls_sensor(sensor::Sensor *sensor) { dsmr_failed_polls_sensor_ = sensor; }
  void set_modbus_total_requests_sensor(sensor::Sensor *sensor) { modbus_total_requests_sensor_ = sensor; }
  void set_modbus_requests_per_minute_sensor(sensor::Sensor *sensor) { modbus_requests_per_minute_sensor_ = sensor; }
  void set_modbus_crc_errors_sensor(sensor::Sensor *sensor) { modbus_crc_errors_sensor_ = sensor; }
  void set_modbus_unsupported_functions_sensor(sensor::Sensor *sensor) { modbus_unsupported_functions_sensor_ = sensor; }
  void set_modbus_illegal_addresses_sensor(sensor::Sensor *sensor) { modbus_illegal_addresses_sensor_ = sensor; }
  void set_modbus_exceptions_sensor(sensor::Sensor *sensor) { modbus_exceptions_sensor_ = sensor; }
  void set_modbus_wrong_slave_ids_sensor(sensor::Sensor *sensor) { modbus_wrong_slave_ids_sensor_ = sensor; }
  void set_last_modbus_function_sensor(sensor::Sensor *sensor) { last_modbus_function_sensor_ = sensor; }
  void set_last_modbus_start_address_sensor(sensor::Sensor *sensor) { last_modbus_start_address_sensor_ = sensor; }
  void set_last_modbus_quantity_sensor(sensor::Sensor *sensor) { last_modbus_quantity_sensor_ = sensor; }
  void set_last_modbus_exception_sensor(sensor::Sensor *sensor) { last_modbus_exception_sensor_ = sensor; }
  void set_seconds_since_last_wallbox_poll_sensor(sensor::Sensor *sensor) {
    seconds_since_last_wallbox_poll_sensor_ = sensor;
  }

  void set_dsmr_api_connected_binary_sensor(binary_sensor::BinarySensor *sensor) { dsmr_api_connected_sensor_ = sensor; }
  void set_dsmr_data_stale_binary_sensor(binary_sensor::BinarySensor *sensor) { dsmr_data_stale_sensor_ = sensor; }
  void set_wallbox_polling_active_binary_sensor(binary_sensor::BinarySensor *sensor) {
    wallbox_polling_active_sensor_ = sensor;
  }
  void set_fail_safe_active_binary_sensor(binary_sensor::BinarySensor *sensor) { fail_safe_active_sensor_ = sensor; }
  void set_last_modbus_crc_ok_binary_sensor(binary_sensor::BinarySensor *sensor) { last_modbus_crc_ok_sensor_ = sensor; }

  void set_dsmr_last_timestamp_text_sensor(text_sensor::TextSensor *sensor) { dsmr_last_timestamp_sensor_ = sensor; }
  void set_dsmr_last_error_text_sensor(text_sensor::TextSensor *sensor) { dsmr_last_error_sensor_ = sensor; }
  void set_selected_source_phase_text_sensor(text_sensor::TextSensor *sensor) { selected_source_phase_sensor_ = sensor; }
  void set_last_modbus_request_summary_text_sensor(text_sensor::TextSensor *sensor) {
    last_modbus_request_summary_sensor_ = sensor;
  }
  void set_last_modbus_frame_hex_text_sensor(text_sensor::TextSensor *sensor) { last_modbus_frame_hex_sensor_ = sensor; }
  void set_last_requested_register_range_text_sensor(text_sensor::TextSensor *sensor) {
    last_requested_register_range_sensor_ = sensor;
  }

 protected:
#ifdef USE_WEBSERVER
  friend class Em112DebugJsonHandler;
  friend class Em112DebugHandler;
#endif
  static void dsmr_task_trampoline(void *arg);
  void dsmr_task();
  void publish_entities_();
  void service_uart_();
  void process_rx_frame_();
  void register_web_handlers_();
#ifdef USE_WEBSERVER
  void handle_debug_json_request_(AsyncWebServerRequest *request);
  void handle_debug_request_(AsyncWebServerRequest *request);
#endif
  void handle_poll_result_(bool ok, const DsmrActualValues &values, uint32_t response_time_ms, const char *error);

  void lock_();
  void unlock_();

  char dsmr_url_[160]{};
  uint32_t poll_interval_ms_{10000};
  MeterRuntimeConfig runtime_config_{};
  ModbusSettings modbus_settings_{};
  bool debug_logging_{false};

  Em112RegisterModel registers_{};
  ModbusRtuSlave modbus_{};
  DebugRing debug_ring_{};
  uint32_t dsmr_success_count_{0};
  uint32_t dsmr_fail_count_{0};
  uint32_t dsmr_response_time_ms_{0};
  char dsmr_last_error_[96]{};
  bool dsmr_task_started_{false};

  uint8_t rx_buffer_[ModbusRtuSlave::MAX_FRAME]{};
  size_t rx_len_{0};
  uint32_t last_rx_byte_ms_{0};
  uint32_t last_publish_ms_{0};
  uint32_t rpm_window_ms_{0};
  uint32_t rpm_window_total_{0};
  float requests_per_minute_{0.0f};

#ifdef ARDUINO
  SemaphoreHandle_t mutex_{nullptr};
#endif

  sensor::Sensor *grid_net_power_sensor_{nullptr};
  sensor::Sensor *grid_import_power_sensor_{nullptr};
  sensor::Sensor *grid_export_power_sensor_{nullptr};
  sensor::Sensor *selected_voltage_sensor_{nullptr};
  sensor::Sensor *selected_current_sensor_{nullptr};
  sensor::Sensor *import_energy_sensor_{nullptr};
  sensor::Sensor *export_energy_sensor_{nullptr};
  sensor::Sensor *apparent_power_sensor_{nullptr};
  sensor::Sensor *reactive_power_sensor_{nullptr};
  sensor::Sensor *power_factor_sensor_{nullptr};
  sensor::Sensor *frequency_sensor_{nullptr};
  sensor::Sensor *dsmr_last_poll_age_sensor_{nullptr};
  sensor::Sensor *dsmr_response_time_sensor_{nullptr};
  sensor::Sensor *dsmr_successful_polls_sensor_{nullptr};
  sensor::Sensor *dsmr_failed_polls_sensor_{nullptr};
  sensor::Sensor *modbus_total_requests_sensor_{nullptr};
  sensor::Sensor *modbus_requests_per_minute_sensor_{nullptr};
  sensor::Sensor *modbus_crc_errors_sensor_{nullptr};
  sensor::Sensor *modbus_unsupported_functions_sensor_{nullptr};
  sensor::Sensor *modbus_illegal_addresses_sensor_{nullptr};
  sensor::Sensor *modbus_exceptions_sensor_{nullptr};
  sensor::Sensor *modbus_wrong_slave_ids_sensor_{nullptr};
  sensor::Sensor *last_modbus_function_sensor_{nullptr};
  sensor::Sensor *last_modbus_start_address_sensor_{nullptr};
  sensor::Sensor *last_modbus_quantity_sensor_{nullptr};
  sensor::Sensor *last_modbus_exception_sensor_{nullptr};
  sensor::Sensor *seconds_since_last_wallbox_poll_sensor_{nullptr};

  binary_sensor::BinarySensor *dsmr_api_connected_sensor_{nullptr};
  binary_sensor::BinarySensor *dsmr_data_stale_sensor_{nullptr};
  binary_sensor::BinarySensor *wallbox_polling_active_sensor_{nullptr};
  binary_sensor::BinarySensor *fail_safe_active_sensor_{nullptr};
  binary_sensor::BinarySensor *last_modbus_crc_ok_sensor_{nullptr};

  text_sensor::TextSensor *dsmr_last_timestamp_sensor_{nullptr};
  text_sensor::TextSensor *dsmr_last_error_sensor_{nullptr};
  text_sensor::TextSensor *selected_source_phase_sensor_{nullptr};
  text_sensor::TextSensor *last_modbus_request_summary_sensor_{nullptr};
  text_sensor::TextSensor *last_modbus_frame_hex_sensor_{nullptr};
  text_sensor::TextSensor *last_requested_register_range_sensor_{nullptr};
};

}  // namespace em112_bridge
}  // namespace esphome
