"""Configuration switches (Bluetooth, multi-target tracking) for the LD2450 UART component."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import CONF_LD2450_UART_ID, LD2450UartComponent, ld2450_uart_ns

DEPENDENCIES = ["ld2450_uart"]

CONF_BLUETOOTH = "bluetooth"
CONF_MULTI_TARGET = "multi_target"

LD2450BluetoothSwitch = ld2450_uart_ns.class_("LD2450BluetoothSwitch", switch.Switch)
LD2450MultiTargetSwitch = ld2450_uart_ns.class_("LD2450MultiTargetSwitch", switch.Switch)

# The switches mirror settings stored inside the radar module. Their state is
# read back from the module after boot and after every command, so there is
# nothing to restore from flash -> restore_mode defaults to DISABLED.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LD2450_UART_ID): cv.use_id(LD2450UartComponent),
        cv.Optional(CONF_BLUETOOTH): switch.switch_schema(
            LD2450BluetoothSwitch,
            icon="mdi:bluetooth",
            entity_category=ENTITY_CATEGORY_CONFIG,
            default_restore_mode="DISABLED",
        ),
        cv.Optional(CONF_MULTI_TARGET): switch.switch_schema(
            LD2450MultiTargetSwitch,
            icon="mdi:account-multiple",
            entity_category=ENTITY_CATEGORY_CONFIG,
            default_restore_mode="DISABLED",
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LD2450_UART_ID])

    if CONF_BLUETOOTH in config:
        sw = await switch.new_switch(config[CONF_BLUETOOTH])
        await cg.register_parented(sw, parent)
        cg.add(parent.set_bluetooth_switch(sw))

    if CONF_MULTI_TARGET in config:
        sw = await switch.new_switch(config[CONF_MULTI_TARGET])
        await cg.register_parented(sw, parent)
        cg.add(parent.set_multi_target_switch(sw))
