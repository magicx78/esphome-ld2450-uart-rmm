"""Presence + per-target present/moving binary sensors for the LD2450 UART component."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    DEVICE_CLASS_MOVING,
    DEVICE_CLASS_OCCUPANCY,
)

from . import CONF_LD2450_UART_ID, LD2450UartComponent

DEPENDENCIES = ["ld2450_uart"]

CONF_PRESENCE = "presence"
CONF_PRESENT = "present"
CONF_MOVING = "moving"

_TARGET_KEYS = ("target_1", "target_2", "target_3")

_TARGET_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PRESENT): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_OCCUPANCY,
        ),
        cv.Optional(CONF_MOVING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_MOVING,
        ),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LD2450_UART_ID): cv.use_id(LD2450UartComponent),
        cv.Optional(CONF_PRESENCE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_OCCUPANCY,
        ),
        cv.Optional("target_1"): _TARGET_SCHEMA,
        cv.Optional("target_2"): _TARGET_SCHEMA,
        cv.Optional("target_3"): _TARGET_SCHEMA,
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LD2450_UART_ID])

    if CONF_PRESENCE in config:
        b = await binary_sensor.new_binary_sensor(config[CONF_PRESENCE])
        cg.add(parent.set_presence_binary_sensor(b))

    for index, key in enumerate(_TARGET_KEYS):
        if key not in config:
            continue
        tconf = config[key]
        if CONF_PRESENT in tconf:
            b = await binary_sensor.new_binary_sensor(tconf[CONF_PRESENT])
            cg.add(parent.set_target_present_binary_sensor(index, b))
        if CONF_MOVING in tconf:
            b = await binary_sensor.new_binary_sensor(tconf[CONF_MOVING])
            cg.add(parent.set_target_moving_binary_sensor(index, b))
