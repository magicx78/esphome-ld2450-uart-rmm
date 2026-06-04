"""ESPHome external component for the HLK-LD2450 radar over UART (with RMM support)."""

import re

import esphome.codegen as cg
import esphome.config_validation as cv
# NOTE: alias the sensor import. This package has a `sensor.py` platform
# submodule; importing it rebinds the package-level `sensor` attribute, which
# would otherwise shadow `esphome.components.sensor` here. The alias avoids that.
from esphome.components import uart
from esphome.components import sensor as core_sensor
from esphome.const import (
    CONF_FILTERS,
    CONF_ID,
    CONF_NAME,
    CONF_THROTTLE,
)

CODEOWNERS = ["@magicx78"]
DEPENDENCIES = ["uart"]
# `sensor` is auto-loaded because the optional `rmm:` block generates sensors
# directly from this component's `to_code`.
AUTO_LOAD = ["sensor"]
MULTI_CONF = True

ld2450_uart_ns = cg.esphome_ns.namespace("ld2450_uart")
LD2450UartComponent = ld2450_uart_ns.class_(
    "LD2450UartComponent", cg.Component, uart.UARTDevice
)

CONF_LD2450_UART_ID = "ld2450_uart_id"

CONF_RMM = "rmm"
CONF_ENABLED = "enabled"
CONF_RADAR_NAME = "radar_name"
CONF_UNIT = "unit"

# Private keys used to carry the RMM-generated, already-validated sensor configs
# from validation (_expand_rmm) into to_code.
_RMM_COORD_SENSORS = "_rmm_coord_sensors"
_RMM_COUNT_SENSOR = "_rmm_count_sensor"


def _valid_radar_name(value):
    value = cv.string_strict(value).lower()
    if not re.match(r"^[a-z0-9_]+$", value):
        raise cv.Invalid(
            "radar_name must contain only lowercase letters, digits and underscores "
            "(it becomes the Home Assistant entity_id prefix, e.g. "
            "'wohnzimmer_ld2450' -> sensor.wohnzimmer_ld2450_target_1_x)"
        )
    return value


RMM_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ENABLED, default=True): cv.boolean,
        cv.Required(CONF_RADAR_NAME): _valid_radar_name,
        cv.Optional(CONF_UNIT, default="mm"): cv.one_of("mm", "cm", lower=True),
    }
)

def _coord_sensor_config(name, unit, multiply):
    """Build and validate a coordinate sensor config named exactly `name`."""
    kwargs = {
        "unit_of_measurement": unit,
        "accuracy_decimals": 1 if multiply is not None else 0,
        "icon": "mdi:radar",
    }
    conf = {CONF_ID: name, CONF_NAME: name}
    if multiply is not None:
        conf[CONF_FILTERS] = [{"multiply": multiply}]
    return core_sensor.sensor_schema(**kwargs)(conf)


def _count_sensor_config(name):
    return core_sensor.sensor_schema(accuracy_decimals=0, icon="mdi:account-group")(
        {CONF_ID: name, CONF_NAME: name}
    )


def _expand_rmm(config):
    """When `rmm:` is enabled, synthesise the RMM-compatible sensor configs.

    Runs during validation so that generated sensor IDs are registered normally.
    Produces, for radar_name R:
      sensor.R_target_1_x / _y ... R_target_3_x / _y
      sensor.R_presence_target_count
    """
    rmm = config.get(CONF_RMM)
    if rmm is None or not rmm[CONF_ENABLED]:
        return config

    name = rmm[CONF_RADAR_NAME]
    unit = rmm[CONF_UNIT]
    multiply = None if unit == "mm" else 0.1  # native values are mm

    coord = []
    for i in range(1, 4):
        for axis in ("x", "y"):
            sname = f"{name}_target_{i}_{axis}"
            coord.append(
                {
                    "axis": axis,
                    "index": i - 1,
                    "config": _coord_sensor_config(sname, unit, multiply),
                }
            )
    config[_RMM_COORD_SENSORS] = coord
    config[_RMM_COUNT_SENSOR] = _count_sensor_config(f"{name}_presence_target_count")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LD2450UartComponent),
            cv.Optional(
                CONF_THROTTLE, default="200ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_RMM): RMM_SCHEMA,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    _expand_rmm,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_throttle(config[CONF_THROTTLE]))

    if _RMM_COORD_SENSORS in config:
        setters = {"x": var.set_x_sensor, "y": var.set_y_sensor}
        for entry in config[_RMM_COORD_SENSORS]:
            s = await core_sensor.new_sensor(entry["config"])
            cg.add(setters[entry["axis"]](entry["index"], s))
        count = await core_sensor.new_sensor(config[_RMM_COUNT_SENSOR])
        cg.add(var.set_target_count_sensor(count))
