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

def _coord_sensor_config(sid, name, unit, multiply):
    """Build and validate a coordinate sensor: HA entity name `name`, id `sid`."""
    kwargs = {
        "unit_of_measurement": unit,
        "accuracy_decimals": 1 if multiply is not None else 0,
        "icon": "mdi:radar",
    }
    conf = {CONF_ID: sid, CONF_NAME: name}
    if multiply is not None:
        conf[CONF_FILTERS] = [{"multiply": multiply}]
    return core_sensor.sensor_schema(**kwargs)(conf)


def _count_sensor_config(sid, name):
    return core_sensor.sensor_schema(accuracy_decimals=0, icon="mdi:account-group")(
        {CONF_ID: sid, CONF_NAME: name}
    )


def _expand_rmm(config):
    """When `rmm:` is enabled, synthesise the RMM-compatible sensor configs.

    IMPORTANT: Home Assistant prefixes every entity_id with the device name, so
    the sensors are given SHORT names (`target_1_x`, ...). Combined with a device
    whose name is `radar_name` this yields the exact ids RMM expects:
      sensor.<radar_name>_target_1_x / _y ... and _presence_target_count
    (_final_validate enforces that the device name matches radar_name.)
    The internal C++ id keeps the radar_name prefix to stay globally unique.
    """
    rmm = config.get(CONF_RMM)
    if rmm is None or not rmm[CONF_ENABLED]:
        return config

    radar = rmm[CONF_RADAR_NAME]
    unit = rmm[CONF_UNIT]
    multiply = None if unit == "mm" else 0.1  # native values are mm

    coord = []
    for i in range(1, 4):
        for axis in ("x", "y"):
            coord.append(
                {
                    "axis": axis,
                    "index": i - 1,
                    "config": _coord_sensor_config(
                        f"{radar}_target_{i}_{axis}", f"target_{i}_{axis}", unit, multiply
                    ),
                }
            )
    config[_RMM_COORD_SENSORS] = coord
    config[_RMM_COUNT_SENSOR] = _count_sensor_config(
        f"{radar}_presence_target_count", "presence_target_count"
    )
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


def _ha_slug(text):
    """Mimic Home Assistant's entity_id slugify (lowercase, non-alnum -> _)."""
    return re.sub(r"[^a-z0-9_]+", "_", text.lower()).strip("_")


def _final_validate(config):
    """Ensure the device name matches radar_name.

    Home Assistant builds entity_id as `<device_name>_<entity_name>`. RMM needs
    `sensor.<radar_name>_target_1_x`, so the device's (friendly) name MUST slugify
    to radar_name. Caught here at config time instead of as a surprise in HA.
    """
    rmm = config.get(CONF_RMM)
    if not rmm or not rmm[CONF_ENABLED]:
        return config
    from esphome.core import CORE

    device = CORE.friendly_name or CORE.name
    slug = _ha_slug(device)
    if slug != rmm[CONF_RADAR_NAME]:
        suggested = rmm[CONF_RADAR_NAME].replace("_", "-")
        raise cv.Invalid(
            f"rmm.radar_name '{rmm[CONF_RADAR_NAME]}' does not match this device's "
            f"name '{device}' (Home Assistant entity_id prefix '{slug}'). Because HA "
            f"prefixes every entity_id with the device name, RMM would see "
            f"'sensor.{slug}_target_1_x' instead. Fix by setting `esphome: name: "
            f"{suggested}` (and no conflicting friendly_name), or set radar_name: "
            f"'{slug}'."
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


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
