"""Per-target and aggregate sensors for the LD2450 UART component."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_DISTANCE,
    CONF_RESOLUTION,
    CONF_SPEED,
    DEVICE_CLASS_DISTANCE,
    DEVICE_CLASS_SPEED,
    STATE_CLASS_MEASUREMENT,
)

from . import CONF_LD2450_UART_ID, LD2450UartComponent

DEPENDENCIES = ["ld2450_uart"]

CONF_X = "x"
CONF_Y = "y"
CONF_TARGET_COUNT = "target_count"

UNIT_MILLIMETER = "mm"
UNIT_MILLIMETER_PER_SECOND = "mm/s"

_TARGET_KEYS = ("target_1", "target_2", "target_3")


def _coordinate_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_MILLIMETER,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_DISTANCE,
        state_class=STATE_CLASS_MEASUREMENT,
        icon="mdi:radar",
    )


_TARGET_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_X): _coordinate_schema(),
        cv.Optional(CONF_Y): _coordinate_schema(),
        cv.Optional(CONF_SPEED): sensor.sensor_schema(
            unit_of_measurement=UNIT_MILLIMETER_PER_SECOND,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_SPEED,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_DISTANCE): sensor.sensor_schema(
            unit_of_measurement=UNIT_MILLIMETER,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DISTANCE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_RESOLUTION): sensor.sensor_schema(
            unit_of_measurement=UNIT_MILLIMETER,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LD2450_UART_ID): cv.use_id(LD2450UartComponent),
        cv.Optional("target_1"): _TARGET_SCHEMA,
        cv.Optional("target_2"): _TARGET_SCHEMA,
        cv.Optional("target_3"): _TARGET_SCHEMA,
        cv.Optional(CONF_TARGET_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:account-group",
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
)

_SETTERS = {
    CONF_X: "set_x_sensor",
    CONF_Y: "set_y_sensor",
    CONF_SPEED: "set_speed_sensor",
    CONF_DISTANCE: "set_distance_sensor",
    CONF_RESOLUTION: "set_resolution_sensor",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LD2450_UART_ID])

    for index, key in enumerate(_TARGET_KEYS):
        if key not in config:
            continue
        target_conf = config[key]
        for field, setter in _SETTERS.items():
            if field in target_conf:
                sens = await sensor.new_sensor(target_conf[field])
                cg.add(getattr(parent, setter)(index, sens))

    if CONF_TARGET_COUNT in config:
        sens = await sensor.new_sensor(config[CONF_TARGET_COUNT])
        cg.add(parent.set_target_count_sensor(sens))
