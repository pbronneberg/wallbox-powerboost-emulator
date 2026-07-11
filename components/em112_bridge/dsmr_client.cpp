#include "dsmr_client.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace esphome {
namespace em112_bridge {

namespace {

void set_error(char *error, size_t error_len, const char *message) {
  if (error == nullptr || error_len == 0)
    return;
  strncpy(error, message, error_len - 1);
  error[error_len - 1] = '\0';
}

const char *skip_ws(const char *p) {
  while (*p && isspace(static_cast<unsigned char>(*p)))
    p++;
  return p;
}

bool extract_json_string(const char *object_start, const char *object_end, const char *key, char *dest, size_t len) {
  if (len == 0)
    return false;
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char *p = object_start;
  while (p < object_end) {
    const char *found = strstr(p, needle);
    if (found == nullptr || found >= object_end)
      return false;
    p = found + strlen(needle);
    p = skip_ws(p);
    if (*p++ != ':')
      continue;
    p = skip_ws(p);
    if (*p != '"')
      return false;
    p++;
    size_t used = 0;
    while (p < object_end && *p && *p != '"') {
      if (*p == '\\' && p + 1 < object_end)
        p++;
      if (used + 1 < len)
        dest[used++] = *p;
      p++;
    }
    dest[used] = '\0';
    return true;
  }
  return false;
}

bool extract_json_value(const char *object_start, const char *object_end, const char *key, char *dest, size_t len) {
  if (len == 0)
    return false;
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char *found = strstr(object_start, needle);
  if (found == nullptr || found >= object_end)
    return false;
  const char *p = found + strlen(needle);
  p = skip_ws(p);
  if (*p++ != ':')
    return false;
  p = skip_ws(p);

  size_t used = 0;
  if (*p == '"') {
    p++;
    while (p < object_end && *p && *p != '"') {
      if (*p == '\\' && p + 1 < object_end)
        p++;
      if (used + 1 < len)
        dest[used++] = *p;
      p++;
    }
  } else {
    while (p < object_end && *p && *p != ',' && *p != '}') {
      if (used + 1 < len)
        dest[used++] = *p;
      p++;
    }
  }
  dest[used] = '\0';
  return true;
}

void set_optional(OptionalFloat *target, const char *raw_value) {
  float parsed = 0.0f;
  if (parse_numeric_value(raw_value, &parsed)) {
    target->available = true;
    target->value = parsed;
  } else {
    target->available = false;
    target->value = 0.0f;
  }
}

void set_optional(OptionalDouble *target, const char *raw_value) {
  if (raw_value == nullptr) {
    target->available = false;
    target->value = 0.0;
    return;
  }
  const char *p = skip_ws(raw_value);
  if (*p == '\0' || strcmp(p, "-") == 0 || strncmp(p, "null", 4) == 0) {
    target->available = false;
    target->value = 0.0;
    return;
  }
  char *end = nullptr;
  const double parsed = strtod(p, &end);
  if (end == p || !isfinite(parsed)) {
    target->available = false;
    target->value = 0.0;
    return;
  }
  end = const_cast<char *>(skip_ws(end));
  target->available = *end == '\0';
  target->value = target->available ? parsed : 0.0;
}

void assign_field(DsmrActualValues *out, const char *name, const char *value) {
  if (strcmp(name, "timestamp") == 0) {
    strncpy(out->timestamp, value, sizeof(out->timestamp) - 1);
    out->timestamp[sizeof(out->timestamp) - 1] = '\0';
  } else if (strcmp(name, "energy_delivered_tariff1") == 0) {
    set_optional(&out->energy_delivered_tariff1, value);
  } else if (strcmp(name, "energy_delivered_tariff2") == 0) {
    set_optional(&out->energy_delivered_tariff2, value);
  } else if (strcmp(name, "energy_returned_tariff1") == 0) {
    set_optional(&out->energy_returned_tariff1, value);
  } else if (strcmp(name, "energy_returned_tariff2") == 0) {
    set_optional(&out->energy_returned_tariff2, value);
  } else if (strcmp(name, "power_delivered") == 0) {
    set_optional(&out->power_delivered, value);
  } else if (strcmp(name, "power_returned") == 0) {
    set_optional(&out->power_returned, value);
  } else if (strcmp(name, "voltage_l1") == 0) {
    set_optional(&out->voltage_l1, value);
  } else if (strcmp(name, "voltage_l2") == 0) {
    set_optional(&out->voltage_l2, value);
  } else if (strcmp(name, "voltage_l3") == 0) {
    set_optional(&out->voltage_l3, value);
  } else if (strcmp(name, "current_l1") == 0) {
    set_optional(&out->current_l1, value);
  } else if (strcmp(name, "current_l2") == 0) {
    set_optional(&out->current_l2, value);
  } else if (strcmp(name, "current_l3") == 0) {
    set_optional(&out->current_l3, value);
  } else if (strcmp(name, "power_delivered_l1") == 0) {
    set_optional(&out->power_delivered_l1, value);
  } else if (strcmp(name, "power_delivered_l2") == 0) {
    set_optional(&out->power_delivered_l2, value);
  } else if (strcmp(name, "power_delivered_l3") == 0) {
    set_optional(&out->power_delivered_l3, value);
  } else if (strcmp(name, "power_returned_l1") == 0) {
    set_optional(&out->power_returned_l1, value);
  } else if (strcmp(name, "power_returned_l2") == 0) {
    set_optional(&out->power_returned_l2, value);
  } else if (strcmp(name, "power_returned_l3") == 0) {
    set_optional(&out->power_returned_l3, value);
  }
}

}  // namespace

bool parse_numeric_value(const char *value, float *out) {
  if (value == nullptr)
    return false;
  const char *p = skip_ws(value);
  if (*p == '\0' || strcmp(p, "-") == 0 || strncmp(p, "null", 4) == 0)
    return false;
  char *end = nullptr;
  const float parsed = strtof(p, &end);
  if (end == p || !isfinite(parsed))
    return false;
  end = const_cast<char *>(skip_ws(end));
  if (*end != '\0')
    return false;
  *out = parsed;
  return true;
}

bool parse_dsmr_actual_json(const char *json, DsmrActualValues *out, char *error, size_t error_len) {
  if (json == nullptr || out == nullptr) {
    set_error(error, error_len, "invalid parser argument");
    return false;
  }
  *out = DsmrActualValues{};

  const char *actual = strstr(json, "\"actual\"");
  if (actual == nullptr) {
    set_error(error, error_len, "missing actual array");
    return false;
  }
  const char *array = strchr(actual, '[');
  if (array == nullptr) {
    set_error(error, error_len, "missing actual array start");
    return false;
  }
  const char *p = array + 1;
  uint16_t seen = 0;
  while (*p) {
    p = skip_ws(p);
    if (*p == ']')
      break;
    if (*p != '{') {
      p++;
      continue;
    }
    const char *object_start = p;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    const char *object_end = nullptr;
    while (*p) {
      if (escaped) {
        escaped = false;
      } else if (*p == '\\') {
        escaped = true;
      } else if (*p == '"') {
        in_string = !in_string;
      } else if (!in_string && *p == '{') {
        depth++;
      } else if (!in_string && *p == '}') {
        depth--;
        if (depth == 0) {
          object_end = p + 1;
          break;
        }
      }
      p++;
    }
    if (object_end == nullptr)
      break;

    char name[48] = {};
    char value[64] = {};
    if (extract_json_string(object_start, object_end, "name", name, sizeof(name)) &&
        extract_json_value(object_start, object_end, "value", value, sizeof(value))) {
      assign_field(out, name, value);
      seen++;
    }
    p = object_end;
  }

  if (seen == 0) {
    set_error(error, error_len, "actual array has no parseable fields");
    return false;
  }
  set_error(error, error_len, "");
  return true;
}

}  // namespace em112_bridge
}  // namespace esphome
