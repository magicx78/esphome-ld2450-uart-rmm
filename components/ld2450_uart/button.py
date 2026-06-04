"""Action buttons (restart, factory reset) for the LD2450 UART component."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import (
    DEVICE_CLASS_RESTART,
    ENTITY_CATEGORY_CONFIG,
)

from . import CONF_LD2450_UART_ID, LD2450UartComponent, ld2450_uart_ns

DEPENDENCIES = ["ld2450_uart"]

CONF_RESTART = "restart"
CONF_FACTORY_RESET = "factory_reset"

LD2450RestartButton = ld2450_uart_ns.class_("LD2450RestartButton", button.Button)
LD2450FactoryResetButton = ld2450_uart_ns.class_(
    "LD2450FactoryResetButton", button.Button
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LD2450_UART_ID): cv.use_id(LD2450UartComponent),
        cv.Optional(CONF_RESTART): button.button_schema(
            LD2450RestartButton,
            device_class=DEVICE_CLASS_RESTART,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:restart",
        ),
        cv.Optional(CONF_FACTORY_RESET): button.button_schema(
            LD2450FactoryResetButton,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:restore-alert",
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LD2450_UART_ID])

    if CONF_RESTART in config:
        b = await button.new_button(config[CONF_RESTART])
        await cg.register_parented(b, parent)

    if CONF_FACTORY_RESET in config:
        b = await button.new_button(config[CONF_FACTORY_RESET])
        await cg.register_parented(b, parent)
