"""Presence binary sensor for the LD2450 UART component."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_OCCUPANCY

from . import CONF_LD2450_UART_ID, LD2450UartComponent

DEPENDENCIES = ["ld2450_uart"]

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(
    device_class=DEVICE_CLASS_OCCUPANCY,
).extend(
    {
        cv.GenerateID(CONF_LD2450_UART_ID): cv.use_id(LD2450UartComponent),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LD2450_UART_ID])
    var = await binary_sensor.new_binary_sensor(config)
    cg.add(parent.set_presence_binary_sensor(var))
