import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, text_sensor, uart
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_HERTZ,
    UNIT_KILOWATT_HOURS,
    UNIT_MILLISECOND,
    UNIT_SECOND,
    UNIT_VOLT,
    UNIT_VOLT_AMPS,
    UNIT_WATT,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor"]

em112_bridge_ns = cg.esphome_ns.namespace("em112_bridge")
Em112Bridge = em112_bridge_ns.class_("Em112Bridge", cg.Component, uart.UARTDevice)

CONF_DSMR_URL = "dsmr_url"
CONF_POLL_INTERVAL = "poll_interval"
CONF_STALE_TIMEOUT = "stale_timeout"
CONF_FAIL_SAFE_IMPORT_POWER = "fail_safe_import_power"
CONF_SOURCE_PHASE = "source_phase"
CONF_METER_PROFILE = "meter_profile"
CONF_MODBUS_SLAVE_ID = "modbus_slave_id"
CONF_STRICT_EXCEPTIONS = "strict_exceptions"
CONF_DEBUG_LOGGING = "debug_logging"

POWER_SENSORS = {
    "grid_net_power": ("set_grid_net_power_sensor", "Grid net power", UNIT_WATT, DEVICE_CLASS_POWER, 1),
    "grid_import_power": ("set_grid_import_power_sensor", "Grid import power", UNIT_WATT, DEVICE_CLASS_POWER, 1),
    "grid_export_power": ("set_grid_export_power_sensor", "Grid export power", UNIT_WATT, DEVICE_CLASS_POWER, 1),
    "apparent_power": ("set_apparent_power_sensor", "Apparent power", UNIT_VOLT_AMPS, None, 1),
    "reactive_power": ("set_reactive_power_sensor", "Reactive power", "var", None, 1),
}

MEASUREMENT_SENSORS = {
    "selected_voltage": ("set_selected_voltage_sensor", UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1),
    "selected_current": ("set_selected_current_sensor", UNIT_AMPERE, DEVICE_CLASS_CURRENT, 3),
    "power_factor": ("set_power_factor_sensor", None, None, 3),
    "frequency": ("set_frequency_sensor", UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, 1),
}

ENERGY_SENSORS = {
    "import_energy": "set_import_energy_sensor",
    "export_energy": "set_export_energy_sensor",
}

DIAGNOSTIC_SENSORS = {
    "dsmr_last_poll_age": ("set_dsmr_last_poll_age_sensor", UNIT_SECOND, DEVICE_CLASS_DURATION, 0),
    "dsmr_response_time": ("set_dsmr_response_time_sensor", UNIT_MILLISECOND, None, 0),
    "dsmr_successful_polls": ("set_dsmr_successful_polls_sensor", None, None, 0),
    "dsmr_failed_polls": ("set_dsmr_failed_polls_sensor", None, None, 0),
    "modbus_total_requests": ("set_modbus_total_requests_sensor", None, None, 0),
    "modbus_requests_per_minute": ("set_modbus_requests_per_minute_sensor", None, None, 0),
    "modbus_crc_errors": ("set_modbus_crc_errors_sensor", None, None, 0),
    "modbus_unsupported_functions": ("set_modbus_unsupported_functions_sensor", None, None, 0),
    "modbus_illegal_addresses": ("set_modbus_illegal_addresses_sensor", None, None, 0),
    "modbus_exceptions": ("set_modbus_exceptions_sensor", None, None, 0),
    "modbus_wrong_slave_ids": ("set_modbus_wrong_slave_ids_sensor", None, None, 0),
    "last_modbus_function": ("set_last_modbus_function_sensor", None, None, 0),
    "last_modbus_start_address": ("set_last_modbus_start_address_sensor", None, None, 0),
    "last_modbus_quantity": ("set_last_modbus_quantity_sensor", None, None, 0),
    "last_modbus_exception": ("set_last_modbus_exception_sensor", None, None, 0),
    "seconds_since_last_wallbox_poll": ("set_seconds_since_last_wallbox_poll_sensor", UNIT_SECOND, DEVICE_CLASS_DURATION, 0),
}

BINARY_SENSORS = {
    "dsmr_api_connected": "set_dsmr_api_connected_binary_sensor",
    "dsmr_data_stale": "set_dsmr_data_stale_binary_sensor",
    "wallbox_polling_active": "set_wallbox_polling_active_binary_sensor",
    "fail_safe_active": "set_fail_safe_active_binary_sensor",
    "last_modbus_crc_ok": "set_last_modbus_crc_ok_binary_sensor",
}

TEXT_SENSORS = {
    "dsmr_last_timestamp": "set_dsmr_last_timestamp_text_sensor",
    "dsmr_last_error": "set_dsmr_last_error_text_sensor",
    "selected_source_phase": "set_selected_source_phase_text_sensor",
    "selected_meter_profile": "set_selected_meter_profile_text_sensor",
    "last_modbus_request_summary": "set_last_modbus_request_summary_text_sensor",
    "last_modbus_frame_hex": "set_last_modbus_frame_hex_text_sensor",
    "last_requested_register_range": "set_last_requested_register_range_text_sensor",
}


def _sensor_schema(unit=None, device_class=None, accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT, **kwargs):
    args = {
        "accuracy_decimals": accuracy_decimals,
        "state_class": state_class,
        **kwargs,
    }
    if unit is not None:
        args["unit_of_measurement"] = unit
    if device_class is not None:
        args["device_class"] = device_class
    return sensor.sensor_schema(**args)


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Em112Bridge),
            cv.Required(CONF_DSMR_URL): cv.string,
            cv.Optional(CONF_POLL_INTERVAL, default="10s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_STALE_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_FAIL_SAFE_IMPORT_POWER, default=7000): cv.float_range(min=0, max=25000),
            cv.Optional(CONF_SOURCE_PHASE, default="l1"): cv.one_of("total", "l1", "l2", "l3", lower=True),
            cv.Optional(CONF_METER_PROFILE, default="em112_pfb"): cv.one_of(
                "em112_pfb", "em330_av5", "em530_av5", lower=True
            ),
            cv.Optional(CONF_MODBUS_SLAVE_ID, default=1): cv.int_range(min=1, max=247),
            cv.Optional(CONF_STRICT_EXCEPTIONS, default=False): cv.boolean,
            cv.Optional(CONF_DEBUG_LOGGING, default=False): cv.boolean,
            **{
                cv.Optional(key): _sensor_schema(unit, device_class, decimals)
                for key, (_setter, _label, unit, device_class, decimals) in POWER_SENSORS.items()
            },
            **{
                cv.Optional(key): _sensor_schema(unit, device_class, decimals)
                for key, (_setter, unit, device_class, decimals) in MEASUREMENT_SENSORS.items()
            },
            **{
                cv.Optional(key): _sensor_schema(
                    UNIT_KILOWATT_HOURS,
                    DEVICE_CLASS_ENERGY,
                    3,
                    state_class=STATE_CLASS_TOTAL_INCREASING,
                )
                for key in ENERGY_SENSORS
            },
            **{
                cv.Optional(key): _sensor_schema(
                    unit,
                    device_class,
                    decimals,
                    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                )
                for key, (_setter, unit, device_class, decimals) in DIAGNOSTIC_SENSORS.items()
            },
            **{
                cv.Optional(key): binary_sensor.binary_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC)
                for key in BINARY_SENSORS
            },
            **{
                cv.Optional(key): text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC)
                for key in TEXT_SENSORS
            },
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_dsmr_url(config[CONF_DSMR_URL]))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL].total_milliseconds))
    cg.add(var.set_stale_timeout(config[CONF_STALE_TIMEOUT].total_milliseconds))
    cg.add(var.set_fail_safe_import_power(config[CONF_FAIL_SAFE_IMPORT_POWER]))
    cg.add(var.set_source_phase_string(config[CONF_SOURCE_PHASE]))
    cg.add(var.set_meter_profile_string(config[CONF_METER_PROFILE]))
    cg.add(var.set_modbus_slave_id(config[CONF_MODBUS_SLAVE_ID]))
    cg.add(var.set_strict_exceptions(config[CONF_STRICT_EXCEPTIONS]))
    cg.add(var.set_debug_logging(config[CONF_DEBUG_LOGGING]))

    sensor_maps = {}
    sensor_maps.update({key: value[0] for key, value in POWER_SENSORS.items()})
    sensor_maps.update({key: value[0] for key, value in MEASUREMENT_SENSORS.items()})
    sensor_maps.update(ENERGY_SENSORS)
    sensor_maps.update({key: value[0] for key, value in DIAGNOSTIC_SENSORS.items()})

    for key, setter in sensor_maps.items():
      if key in config:
        sens = await sensor.new_sensor(config[key])
        cg.add(getattr(var, setter)(sens))

    for key, setter in BINARY_SENSORS.items():
      if key in config:
        sens = await binary_sensor.new_binary_sensor(config[key])
        cg.add(getattr(var, setter)(sens))

    for key, setter in TEXT_SENSORS.items():
      if key in config:
        sens = await text_sensor.new_text_sensor(config[key])
        cg.add(getattr(var, setter)(sens))
