#include "em112_bridge.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

#include <lwip/netdb.h>
#include <lwip/sockets.h>

#ifdef USE_WEBSERVER
#include "esphome/components/web_server_base/web_server_base.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace esphome {
namespace em112_bridge {

static const char *const TAG = "em112_bridge";

namespace {

template<typename T> void publish(sensor::Sensor *sensor, T value) {
  if (sensor != nullptr)
    sensor->publish_state(static_cast<float>(value));
}

void publish_binary(binary_sensor::BinarySensor *sensor, bool value) {
  if (sensor != nullptr)
    sensor->publish_state(value);
}

void publish_text(text_sensor::TextSensor *sensor, const char *value) {
  if (sensor != nullptr)
    sensor->publish_state(value == nullptr ? "" : value);
}

uint32_t now_ms() { return millis(); }

bool parse_http_url(const char *url, char *host, size_t host_len, uint16_t *port, char *path, size_t path_len) {
  if (url == nullptr || host == nullptr || port == nullptr || path == nullptr || host_len == 0 || path_len == 0)
    return false;
  const char *p = url;
  const char prefix[] = "http://";
  if (strncmp(p, prefix, strlen(prefix)) == 0)
    p += strlen(prefix);
  const char *slash = strchr(p, '/');
  const char *host_end = slash == nullptr ? p + strlen(p) : slash;
  const char *colon = static_cast<const char *>(memchr(p, ':', host_end - p));
  const char *copy_end = colon == nullptr ? host_end : colon;
  const size_t copy_len = copy_end > p ? static_cast<size_t>(copy_end - p) : 0;
  if (copy_len == 0 || copy_len >= host_len)
    return false;
  memcpy(host, p, copy_len);
  host[copy_len] = '\0';
  *port = 80;
  if (colon != nullptr) {
    const int parsed_port = atoi(colon + 1);
    if (parsed_port <= 0 || parsed_port > 65535)
      return false;
    *port = static_cast<uint16_t>(parsed_port);
  }
  const char *path_start = slash == nullptr ? "/" : slash;
  strncpy(path, path_start, path_len - 1);
  path[path_len - 1] = '\0';
  return true;
}

bool http_get_body(const char *url, String *body, uint32_t *elapsed_ms, char *error, size_t error_len) {
  char host[96]{};
  char path[128]{};
  uint16_t port = 80;
  if (!parse_http_url(url, host, sizeof(host), &port, path, sizeof(path))) {
    snprintf(error, error_len, "invalid DSMR URL");
    return false;
  }

  const uint32_t start = now_ms();

  struct addrinfo hints {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char port_text[8];
  snprintf(port_text, sizeof(port_text), "%u", port);
  struct addrinfo *result = nullptr;
  if (getaddrinfo(host, port_text, &hints, &result) != 0 || result == nullptr) {
    snprintf(error, error_len, "DNS lookup failed");
    return false;
  }

  int sock = -1;
  for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock < 0)
      continue;
    struct timeval timeout {};
    timeout.tv_sec = 2;
    timeout.tv_usec = 500000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
      break;
    close(sock);
    sock = -1;
  }
  freeaddrinfo(result);

  if (sock < 0) {
    snprintf(error, error_len, "connect failed");
    return false;
  }

  char request[320];
  const int request_len = snprintf(request, sizeof(request),
                                   "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nAccept: application/json\r\n\r\n",
                                   path, host);
  if (request_len <= 0 || request_len >= static_cast<int>(sizeof(request)) ||
      send(sock, request, request_len, 0) != request_len) {
    close(sock);
    snprintf(error, error_len, "send failed");
    return false;
  }

  String header;
  bool headers_done = false;
  int status = 0;
  body->remove(0);
  const uint32_t deadline = start + 3000U;
  char buffer[256];
  while (static_cast<int32_t>(now_ms() - deadline) <= 0) {
    const int received = recv(sock, buffer, sizeof(buffer), 0);
    if (received == 0)
      break;
    if (received < 0)
      break;
    for (int i = 0; i < received; i++) {
      const char c = buffer[i];
      if (!headers_done) {
        header += c;
        if (header.endsWith("\r\n\r\n")) {
          headers_done = true;
          int first_space = header.indexOf(' ');
          if (first_space > 0)
            status = header.substring(first_space + 1, first_space + 4).toInt();
        }
      } else if (body->length() < 8192) {
        *body += c;
      }
    }
  }
  close(sock);
  *elapsed_ms = now_ms() - start;
  if (status != 200) {
    snprintf(error, error_len, "HTTP status %d", status);
    return false;
  }
  if (!headers_done || body->isEmpty()) {
    snprintf(error, error_len, "empty HTTP response");
    return false;
  }
  return true;
}

}  // namespace

void Em112Bridge::setup() {
#ifdef ARDUINO
  this->mutex_ = xSemaphoreCreateMutex();
#endif
  this->runtime_config_.source_phase = SourcePhase::L1;
  this->runtime_config_.fail_safe_import_power_w = 7000.0f;
  this->runtime_config_.stale_timeout_s = 30;
  this->registers_.set_stale(now_ms(), this->runtime_config_);
  this->rpm_window_ms_ = now_ms();

  this->register_web_handlers_();

#ifdef ARDUINO
  if (!this->dsmr_task_started_) {
    BaseType_t ok = xTaskCreatePinnedToCore(&Em112Bridge::dsmr_task_trampoline, "em112_dsmr", 8192, this, 1, nullptr, 0);
    this->dsmr_task_started_ = ok == pdPASS;
    if (!this->dsmr_task_started_)
      ESP_LOGE(TAG, "Failed to start DSMR polling task");
  }
#endif
}

void Em112Bridge::loop() {
#ifdef USE_WEBSERVER
  if (!this->web_handlers_registered_)
    this->register_web_handlers_();
#endif

  this->service_uart_();

  const uint32_t now = now_ms();
  this->lock_();
  const MeterSnapshot before = this->registers_.snapshot();
  if (!before.data_stale && before.last_success_ms > 0 &&
      (now - before.last_success_ms) > (this->runtime_config_.stale_timeout_s * 1000U)) {
    this->registers_.set_stale(now, this->runtime_config_);
  }
  this->unlock_();

  if (now - this->last_publish_ms_ >= 1000U) {
    this->last_publish_ms_ = now;
    this->publish_entities_();
  }
}

void Em112Bridge::set_dsmr_url(const char *url) {
  this->lock_();
  strncpy(this->dsmr_url_, url == nullptr ? "" : url, sizeof(this->dsmr_url_) - 1);
  this->dsmr_url_[sizeof(this->dsmr_url_) - 1] = '\0';
  this->unlock_();
}

void Em112Bridge::set_source_phase_string(const char *phase) {
  this->runtime_config_.source_phase = source_phase_from_string(phase, this->runtime_config_.source_phase);
}

void Em112Bridge::lock_() {
#ifdef ARDUINO
  if (this->mutex_ != nullptr)
    xSemaphoreTake(this->mutex_, portMAX_DELAY);
#endif
}

void Em112Bridge::unlock_() {
#ifdef ARDUINO
  if (this->mutex_ != nullptr)
    xSemaphoreGive(this->mutex_);
#endif
}

void Em112Bridge::service_uart_() {
  uint8_t byte = 0;
  while (this->available()) {
    if (!this->read_byte(&byte))
      break;
    if (this->rx_len_ < sizeof(this->rx_buffer_)) {
      this->rx_buffer_[this->rx_len_++] = byte;
      this->last_rx_byte_ms_ = now_ms();
    } else {
      this->rx_len_ = 0;
    }
  }

  if (this->rx_len_ > 0 && now_ms() - this->last_rx_byte_ms_ >= 5U)
    this->process_rx_frame_();
}

void Em112Bridge::process_rx_frame_() {
  uint8_t response[ModbusRtuSlave::MAX_FRAME]{};
  DebugRingEntry entry{};

  this->lock_();
  const size_t response_len = this->modbus_.process_frame(this->rx_buffer_, this->rx_len_, response, sizeof(response),
                                                          this->registers_, this->modbus_settings_, now_ms(), &entry);
  this->debug_ring_.push(entry);
  this->unlock_();

  if (response_len > 0)
    this->write_array(response, response_len);

  if (this->debug_logging_) {
    ESP_LOGD(TAG, "Modbus %s slave=%u fc=0x%02X start=0x%04X qty=%u response=%u", entry.note, entry.slave_id,
             entry.function_code, entry.start_address, entry.quantity, entry.response_byte_count);
  }
  this->rx_len_ = 0;
}

void Em112Bridge::dsmr_task_trampoline(void *arg) {
  static_cast<Em112Bridge *>(arg)->dsmr_task();
}

void Em112Bridge::dsmr_task() {
#ifdef ARDUINO
  for (;;) {
    char url[sizeof(this->dsmr_url_)]{};
    uint32_t interval = 10000;
    this->lock_();
    strncpy(url, this->dsmr_url_, sizeof(url) - 1);
    interval = this->poll_interval_ms_;
    this->unlock_();

    if (!network::is_connected() || url[0] == '\0') {
      DsmrActualValues empty{};
      this->handle_poll_result_(false, empty, 0, network::is_connected() ? "missing DSMR URL" : "network not connected");
      vTaskDelay(pdMS_TO_TICKS(interval));
      continue;
    }

    bool ok = false;
    DsmrActualValues parsed{};
    char error[96]{};
    uint32_t elapsed = 0;
    String payload;
    if (http_get_body(url, &payload, &elapsed, error, sizeof(error))) {
      ok = parse_dsmr_actual_json(payload.c_str(), &parsed, error, sizeof(error));
      this->handle_poll_result_(ok, parsed, elapsed, ok ? "" : error);
    } else {
      this->handle_poll_result_(false, parsed, elapsed, error);
    }

    vTaskDelay(pdMS_TO_TICKS(interval));
  }
#endif
}

void Em112Bridge::handle_poll_result_(bool ok, const DsmrActualValues &values, uint32_t response_time_ms,
                                      const char *error) {
  this->lock_();
  this->dsmr_response_time_ms_ = response_time_ms;
  if (ok) {
    this->dsmr_success_count_++;
    this->dsmr_last_error_[0] = '\0';
    this->registers_.update_from_dsmr(values, this->runtime_config_, now_ms(), true);
  } else {
    this->dsmr_fail_count_++;
    strncpy(this->dsmr_last_error_, error == nullptr ? "unknown DSMR error" : error, sizeof(this->dsmr_last_error_) - 1);
    this->dsmr_last_error_[sizeof(this->dsmr_last_error_) - 1] = '\0';
    const MeterSnapshot snapshot = this->registers_.snapshot();
    if (snapshot.last_success_ms == 0 ||
        now_ms() - snapshot.last_success_ms > (this->runtime_config_.stale_timeout_s * 1000U)) {
      this->registers_.set_stale(now_ms(), this->runtime_config_);
    }
  }
  this->unlock_();
}

void Em112Bridge::publish_entities_() {
  this->lock_();
  const MeterSnapshot snapshot = this->registers_.snapshot();
  const ModbusCounters counters = this->modbus_.counters();
  DebugRingEntry latest{};
  const bool has_latest = this->debug_ring_.get_newest(0, &latest);
  const uint32_t success = this->dsmr_success_count_;
  const uint32_t failed = this->dsmr_fail_count_;
  const uint32_t response_time = this->dsmr_response_time_ms_;
  char last_error[sizeof(this->dsmr_last_error_)]{};
  strncpy(last_error, this->dsmr_last_error_, sizeof(last_error) - 1);
  const SourcePhase phase = this->runtime_config_.source_phase;
  this->unlock_();

  const uint32_t now = now_ms();
  if (now - this->rpm_window_ms_ >= 60000U) {
    this->requests_per_minute_ = static_cast<float>(counters.total_requests - this->rpm_window_total_);
    this->rpm_window_total_ = counters.total_requests;
    this->rpm_window_ms_ = now;
  }

  publish(this->grid_net_power_sensor_, snapshot.grid_net_power_w);
  publish(this->grid_import_power_sensor_, snapshot.grid_import_power_w);
  publish(this->grid_export_power_sensor_, snapshot.grid_export_power_w);
  publish(this->selected_voltage_sensor_, snapshot.selected_voltage_v);
  publish(this->selected_current_sensor_, snapshot.selected_current_a);
  publish(this->import_energy_sensor_, snapshot.import_energy_kwh);
  publish(this->export_energy_sensor_, snapshot.export_energy_kwh);
  publish(this->apparent_power_sensor_, snapshot.apparent_power_va);
  publish(this->reactive_power_sensor_, snapshot.reactive_power_var);
  publish(this->power_factor_sensor_, snapshot.power_factor);
  publish(this->frequency_sensor_, snapshot.frequency_hz);
  publish(this->dsmr_last_poll_age_sensor_, snapshot.last_success_ms == 0 ? -1 : (now - snapshot.last_success_ms) / 1000);
  publish(this->dsmr_response_time_sensor_, response_time);
  publish(this->dsmr_successful_polls_sensor_, success);
  publish(this->dsmr_failed_polls_sensor_, failed);
  publish(this->modbus_total_requests_sensor_, counters.total_requests);
  publish(this->modbus_requests_per_minute_sensor_, this->requests_per_minute_);
  publish(this->modbus_crc_errors_sensor_, counters.crc_errors);
  publish(this->modbus_unsupported_functions_sensor_, counters.unsupported_functions);
  publish(this->modbus_illegal_addresses_sensor_, counters.illegal_addresses);
  publish(this->modbus_exceptions_sensor_, counters.exceptions);
  publish(this->modbus_wrong_slave_ids_sensor_, counters.wrong_slave_ids);
  publish(this->last_modbus_function_sensor_, counters.last_function_code);
  publish(this->last_modbus_start_address_sensor_, counters.last_start_address);
  publish(this->last_modbus_quantity_sensor_, counters.last_quantity);
  publish(this->last_modbus_exception_sensor_, counters.last_exception_code);
  publish(this->seconds_since_last_wallbox_poll_sensor_,
          counters.last_wallbox_poll_ms == 0 ? -1 : (now - counters.last_wallbox_poll_ms) / 1000);

  publish_binary(this->dsmr_api_connected_sensor_, snapshot.api_connected);
  publish_binary(this->dsmr_data_stale_sensor_, snapshot.data_stale);
  publish_binary(this->wallbox_polling_active_sensor_,
                 counters.last_wallbox_poll_ms != 0 && now - counters.last_wallbox_poll_ms < 30000U);
  publish_binary(this->fail_safe_active_sensor_, snapshot.fail_safe_active);
  publish_binary(this->last_modbus_crc_ok_sensor_, counters.last_crc_ok);

  publish_text(this->dsmr_last_timestamp_sensor_, snapshot.timestamp);
  publish_text(this->dsmr_last_error_sensor_, last_error);
  publish_text(this->selected_source_phase_sensor_, source_phase_to_string(phase));

  if (has_latest) {
    char summary[96]{};
    snprintf(summary, sizeof(summary), "%s slave=%u FC%02u 0x%04X len %u", latest.note, latest.slave_id,
             latest.function_code, latest.start_address, latest.quantity);
    publish_text(this->last_modbus_request_summary_sensor_, summary);
    publish_text(this->last_modbus_frame_hex_sensor_, latest.frame_hex);
    char range[48]{};
    snprintf(range, sizeof(range), "FC%02u 0x%04X len %u", latest.function_code, latest.start_address, latest.quantity);
    publish_text(this->last_requested_register_range_sensor_, range);
  }
}

#ifdef USE_WEBSERVER
class Em112DebugJsonHandler : public AsyncWebHandler {
 public:
  explicit Em112DebugJsonHandler(Em112Bridge *parent) : parent_(parent) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char url[AsyncWebServerRequest::URL_BUF_SIZE]{};
    const auto request_url = request->url_to(url);
    return request->method() == HTTP_GET && (request_url == "/debug.json" || request_url == "/debug.json/");
  }
  void handleRequest(AsyncWebServerRequest *request) override { this->parent_->handle_debug_json_request_(request); }

 protected:
  Em112Bridge *parent_;
};

class Em112DebugHandler : public AsyncWebHandler {
 public:
  explicit Em112DebugHandler(Em112Bridge *parent) : parent_(parent) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char url[AsyncWebServerRequest::URL_BUF_SIZE]{};
    const auto request_url = request->url_to(url);
    return request->method() == HTTP_GET && (request_url == "/debug" || request_url == "/debug/");
  }
  void handleRequest(AsyncWebServerRequest *request) override { this->parent_->handle_debug_request_(request); }

 protected:
  Em112Bridge *parent_;
};
#endif

void Em112Bridge::register_web_handlers_() {
#ifdef USE_WEBSERVER
  if (this->web_handlers_registered_)
    return;
  if (web_server_base::global_web_server_base == nullptr ||
      web_server_base::global_web_server_base->get_server() == nullptr)
    return;

  web_server_base::global_web_server_base->add_handler(new Em112DebugJsonHandler(this));
  web_server_base::global_web_server_base->add_handler(new Em112DebugHandler(this));
  this->web_handlers_registered_ = true;
#endif
}

#ifdef USE_WEBSERVER
namespace {

void write_json_escaped(AsyncResponseStream *out, const char *text) {
  if (out == nullptr || text == nullptr)
    return;
  for (const char *p = text; *p != '\0'; p++) {
    switch (*p) {
      case '\\':
        out->print("\\\\");
        break;
      case '"':
        out->print("\\\"");
        break;
      case '\n':
        out->print("\\n");
        break;
      case '\r':
        out->print("\\r");
        break;
      case '\t':
        out->print("\\t");
        break;
      default:
        out->write(static_cast<uint8_t>(*p));
        break;
    }
  }
}

void write_html_escaped(AsyncResponseStream *out, const char *text) {
  if (out == nullptr || text == nullptr)
    return;
  for (const char *p = text; *p != '\0'; p++) {
    switch (*p) {
      case '&':
        out->print("&amp;");
        break;
      case '<':
        out->print("&lt;");
        break;
      case '>':
        out->print("&gt;");
        break;
      case '"':
        out->print("&quot;");
        break;
      case '\'':
        out->print("&#39;");
        break;
      default:
        out->write(static_cast<uint8_t>(*p));
        break;
    }
  }
}

}  // namespace

void Em112Bridge::handle_debug_json_request_(AsyncWebServerRequest *request) {
  this->lock_();
  const MeterSnapshot snapshot = this->registers_.snapshot();
  const ModbusCounters counters = this->modbus_.counters();
  const size_t count = this->debug_ring_.size();
  this->unlock_();

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->print("{\"project\":\"wallbox-powerboost-emulator\",\"meter_profile\":\"EM112 PF.B\",");
  response->print("\"dsmr\":{\"net_power_w\":");
  response->printf("%.3f", snapshot.grid_net_power_w);
  response->print(",\"voltage_v\":");
  response->printf("%.3f", snapshot.selected_voltage_v);
  response->print(",\"current_a\":");
  response->printf("%.3f", snapshot.selected_current_a);
  response->print(",\"stale\":");
  response->print(snapshot.data_stale ? "true" : "false");
  response->print(",\"fail_safe\":");
  response->print(snapshot.fail_safe_active ? "true" : "false");
  response->print(",\"timestamp\":\"");
  write_json_escaped(response, snapshot.timestamp);
  response->print("\"},\"modbus\":{\"total_requests\":");
  response->print(counters.total_requests);
  response->print(",\"crc_errors\":");
  response->print(counters.crc_errors);
  response->print(",\"wrong_slave_ids\":");
  response->print(counters.wrong_slave_ids);
  response->print("},\"requests\":[");
  for (size_t i = 0; i < count; i++) {
    DebugRingEntry entry{};
    this->lock_();
    const bool ok = this->debug_ring_.get_newest(i, &entry);
    this->unlock_();
    if (!ok)
      break;
    if (i > 0)
      response->print(",");
    response->print("{\"timestamp_ms\":");
    response->print(entry.timestamp_ms);
    response->print(",\"slave_id\":");
    response->print(entry.slave_id);
    response->print(",\"function_code\":");
    response->print(entry.function_code);
    response->print(",\"start_address\":");
    response->print(entry.start_address);
    response->print(",\"quantity\":");
    response->print(entry.quantity);
    response->print(",\"crc_ok\":");
    response->print(entry.crc_ok ? "true" : "false");
    response->print(",\"exception_code\":");
    response->print(entry.exception_code);
    response->print(",\"response_byte_count\":");
    response->print(entry.response_byte_count);
    response->print(",\"frame_hex\":\"");
    write_json_escaped(response, entry.frame_hex);
    response->print("\",\"note\":\"");
    write_json_escaped(response, entry.note);
    response->print("\"}");
  }
  response->print("]}");
  request->send(response);
}

void Em112Bridge::handle_debug_request_(AsyncWebServerRequest *request) {
  this->lock_();
  const MeterSnapshot snapshot = this->registers_.snapshot();
  const ModbusCounters counters = this->modbus_.counters();
  const size_t count = this->debug_ring_.size();
  this->unlock_();

  AsyncResponseStream *response = request->beginResponseStream("text/html");
  response->print("<!doctype html><html><head><meta charset='utf-8'><title>wallbox-powerboost-emulator</title>");
  response->print("<style>body{font-family:sans-serif;margin:24px}table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:4px 8px}code{background:#eee;padding:2px 4px}</style>");
  response->print("</head><body><h1>wallbox-powerboost-emulator</h1>");
  response->print("<p>Experimental unofficial Carlo Gavazzi EM112 PF.B emulator. Not for billing, fiscal, reimbursement, MID, or legal metrology use.</p>");
  response->print("<h2>DSMR</h2><table><tr><th>Net power W</th><td>");
  response->printf("%.3f", snapshot.grid_net_power_w);
  response->print("</td></tr><tr><th>Voltage V</th><td>");
  response->printf("%.3f", snapshot.selected_voltage_v);
  response->print("</td></tr><tr><th>Current A</th><td>");
  response->printf("%.3f", snapshot.selected_current_a);
  response->print("</td></tr><tr><th>Import kWh</th><td>");
  response->printf("%.3f", snapshot.import_energy_kwh);
  response->print("</td></tr><tr><th>Export kWh</th><td>");
  response->printf("%.3f", snapshot.export_energy_kwh);
  response->print("</td></tr><tr><th>Stale</th><td>");
  response->print(snapshot.data_stale ? "true" : "false");
  response->print("</td></tr><tr><th>Fail-safe</th><td>");
  response->print(snapshot.fail_safe_active ? "true" : "false");
  response->print("</td></tr><tr><th>Timestamp</th><td>");
  write_html_escaped(response, snapshot.timestamp);
  response->print("</td></tr></table><h2>Modbus Counters</h2><table><tr><th>Total</th><td>");
  response->print(counters.total_requests);
  response->print("</td></tr><tr><th>CRC errors</th><td>");
  response->print(counters.crc_errors);
  response->print("</td></tr><tr><th>Wrong slave IDs</th><td>");
  response->print(counters.wrong_slave_ids);
  response->print("</td></tr></table><h2>Recent Requests</h2><table><tr><th>ms</th><th>slave</th><th>fc</th><th>start</th><th>qty</th><th>crc</th><th>exc</th><th>bytes</th><th>note</th><th>frame</th></tr>");
  for (size_t i = 0; i < count; i++) {
    DebugRingEntry entry{};
    this->lock_();
    const bool ok = this->debug_ring_.get_newest(i, &entry);
    this->unlock_();
    if (!ok)
      break;
    char hex[8];
    snprintf(hex, sizeof(hex), "%04X", entry.start_address);
    response->print("<tr><td>");
    response->print(entry.timestamp_ms);
    response->print("</td><td>");
    response->print(entry.slave_id);
    response->print("</td><td>");
    response->print(entry.function_code);
    response->print("</td><td>0x");
    response->print(hex);
    response->print("</td><td>");
    response->print(entry.quantity);
    response->print("</td><td>");
    response->print(entry.crc_ok ? "ok" : "bad");
    response->print("</td><td>");
    response->print(entry.exception_code);
    response->print("</td><td>");
    response->print(entry.response_byte_count);
    response->print("</td><td>");
    write_html_escaped(response, entry.note);
    response->print("</td><td><code>");
    write_html_escaped(response, entry.frame_hex);
    response->print("</code></td></tr>");
  }
  response->print("</table><p><a href='/debug.json'>debug.json</a></p></body></html>");
  request->send(response);
}
#endif

}  // namespace em112_bridge
}  // namespace esphome
