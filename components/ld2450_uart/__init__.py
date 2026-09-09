"""ESPHome external component for the HLK-LD2450 radar over UART (with RMM support)."""

import re
import unicodedata

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
# NOTE: alias the sensor import. This package has a `sensor.py` platform
# submodule; importing it rebinds the package-level `sensor` attribute, which
# would otherwise shadow `esphome.components.sensor` here. The alias avoids that.
from esphome.components import uart
from esphome.components import sensor as core_sensor
from esphome.const import (
    CONF_ESPHOME,
    CONF_FILTERS,
    CONF_ID,
    CONF_NAME,
    CONF_NAME_ADD_MAC_SUFFIX,
    CONF_PLATFORM,
    CONF_THROTTLE,
)
from esphome.core import CORE

CODEOWNERS = ["@magicx78"]
DEPENDENCIES = ["uart"]
# `sensor` is auto-loaded because the optional `rmm:` block generates sensors
# directly from this component's `to_code`.
AUTO_LOAD = ["sensor"]
MULTI_CONF = True

DOMAIN = "ld2450_uart"

ld2450_uart_ns = cg.esphome_ns.namespace("ld2450_uart")
LD2450UartComponent = ld2450_uart_ns.class_(
    "LD2450UartComponent", cg.Component, uart.UARTDevice
)

CONF_LD2450_UART_ID = "ld2450_uart_id"

CONF_RMM = "rmm"
CONF_ENABLED = "enabled"
CONF_RADAR_NAME = "radar_name"
CONF_UNIT = "unit"

# Sensor-platform keys that `rmm:` also generates (see _final_validate).
TARGET_KEYS = ("target_1", "target_2", "target_3")
CONF_TARGET_COUNT = "target_count"

# Private keys used to carry the RMM-generated, already-validated sensor configs
# from validation (_expand_rmm) into to_code.
_RMM_COORD_SENSORS = "_rmm_coord_sensors"
_RMM_COUNT_SENSOR = "_rmm_count_sensor"

# Must also be a valid ESPHome ID prefix (the generated sensor IDs are
# `<radar_name>_target_1_x`), hence no leading digit.
_RADAR_NAME_RE = re.compile(r"^[a-z_][a-z0-9_]*$")


def _ha_slug(text):
    """Slugify the way Home Assistant builds entity_ids.

    HA uses python-slugify, which transliterates first (ü→u, ß→ss, é→e) and
    then replaces every other non-alphanumeric run with `_`. Use the very same
    library when it is importable; otherwise fall back to a Unicode
    decomposition that covers the common accented characters.
    """
    try:
        from slugify import slugify as ha_slugify
    except ImportError:
        ha_slugify = None
    if ha_slugify is not None:
        return ha_slugify(text, separator="_")
    text = text.replace("ß", "ss").replace("ẞ", "SS")
    text = unicodedata.normalize("NFKD", text)
    text = "".join(c for c in text if not unicodedata.combining(c))
    return re.sub(r"[^a-z0-9_]+", "_", text.lower()).strip("_")


def _device_name():
    """The name Home Assistant will register the ESPHome device under."""
    return CORE.friendly_name or CORE.name


def _valid_radar_name(value):
    value = cv.string_strict(value).lower()
    if not _RADAR_NAME_RE.match(value):
        raise cv.Invalid(
            "radar_name must start with a letter or underscore and contain only "
            "lowercase letters, digits and underscores (it becomes the Home "
            "Assistant entity_id prefix and part of the generated ESPHome IDs, "
            "e.g. 'wohnzimmer_ld2450' -> sensor.wohnzimmer_ld2450_target_1_x)"
        )
    return value


RMM_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ENABLED, default=True): cv.boolean,
        # Defaults to the slug of the device name, which is what Home Assistant
        # prefixes the entity_ids with anyway.
        cv.Optional(CONF_RADAR_NAME): _valid_radar_name,
        cv.Optional(CONF_UNIT, default="mm"): cv.one_of("mm", "cm", lower=True),
        # Extra sensor filters for the six coordinate sensors (e.g. delta,
        # throttle_with_priority). Validated by the sensor schema below.
        cv.Optional(CONF_FILTERS, default=[]): cv.ensure_list(),
    }
)


def _coord_sensor_config(sid, name, unit, multiply, extra_filters):
    """Build and validate a coordinate sensor: HA entity name `name`, id `sid`."""
    kwargs = {
        "unit_of_measurement": unit,
        "accuracy_decimals": 1 if multiply is not None else 0,
        "icon": "mdi:radar",
    }
    conf = {CONF_ID: sid, CONF_NAME: name}
    filters = []
    if multiply is not None:
        filters.append({"multiply": multiply})
    filters.extend(extra_filters)
    if filters:
        conf[CONF_FILTERS] = filters
    return core_sensor.sensor_schema(**kwargs)(conf)


def _count_sensor_config(sid, name):
    return core_sensor.sensor_schema(accuracy_decimals=0, icon="mdi:account-group")(
        {CONF_ID: sid, CONF_NAME: name}
    )


def _expand_rmm(config):
    """When `rmm:` is enabled, synthesise the RMM-compatible sensor configs.

    IMPORTANT: Home Assistant prefixes every entity_id with the device name, so
    the sensors are given SHORT names (`target_1_x`, ...). Combined with a device
    whose name slugifies to `radar_name` this yields the exact ids RMM expects:
      sensor.<radar_name>_target_1_x / _y ... and _presence_target_count
    (_final_validate enforces that the device name matches radar_name.)
    The internal C++ id keeps the radar_name prefix to stay globally unique.
    """
    rmm = config.get(CONF_RMM)
    if rmm is None or not rmm[CONF_ENABLED]:
        return config

    if CONF_RADAR_NAME not in rmm:
        derived = _ha_slug(_device_name())
        if not _RADAR_NAME_RE.match(derived):
            raise cv.Invalid(
                f"Cannot derive rmm.radar_name from the device name "
                f"'{_device_name()}' (its Home Assistant slug '{derived}' is not "
                f"a valid radar name). Rename the device so that it starts with "
                f"a letter, e.g. `esphome: name: radar-{derived}`.",
                path=[CONF_RMM],
            )
        rmm[CONF_RADAR_NAME] = derived

    radar = rmm[CONF_RADAR_NAME]
    unit = rmm[CONF_UNIT]
    multiply = None if unit == "mm" else 0.1  # native values are mm

    coord = []
    try:
        for i in range(1, 4):
            for axis in ("x", "y"):
                coord.append(
                    {
                        "axis": axis,
                        "index": i - 1,
                        "config": _coord_sensor_config(
                            f"{radar}_target_{i}_{axis}",
                            f"target_{i}_{axis}",
                            unit,
                            multiply,
                            rmm[CONF_FILTERS],
                        ),
                    }
                )
        config[_RMM_COORD_SENSORS] = coord
        config[_RMM_COUNT_SENSOR] = _count_sensor_config(
            f"{radar}_presence_target_count", "presence_target_count"
        )
    except cv.Invalid as err:
        # ESPHome's duplicate-entity check trips here when a second rmm-enabled
        # instance, or a user sensor named e.g. "Target 1 X", already claimed the
        # short RMM name. Explain the cause instead of the bare duplicate error.
        if "Duplicate" not in str(err):
            raise
        raise cv.Invalid(
            f"rmm: generates the sensors target_1_x .. target_3_y and "
            f"presence_target_count for '{radar}', but one of these entity names "
            "already exists on this device. Only one ld2450_uart instance per "
            "device can enable rmm: (Home Assistant prefixes every entity with the "
            "same device name), and sensor: entries must not reuse these names. "
            f"Original error: {err.msg}",
            path=[CONF_RMM],
        ) from err
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


def _same_id(a, b):
    return getattr(a, "id", a) == getattr(b, "id", b)


def _final_validate(config):
    """Cross-check the `rmm:` naming contract against the whole configuration.

    Home Assistant builds entity_ids as `<device name> <entity name>` (slugified),
    so RMM's `sensor.<radar_name>_target_1_x` only comes out if the device name
    slugifies to radar_name, no MAC suffix is appended, only one radar per device
    uses rmm, and nothing else claims the same sensor slots.
    """
    rmm = config.get(CONF_RMM)
    if not rmm or not rmm[CONF_ENABLED]:
        return config

    full = fv.full_config.get()
    radar = rmm[CONF_RADAR_NAME]

    if full.get(CONF_ESPHOME, {}).get(CONF_NAME_ADD_MAC_SUFFIX):
        raise cv.Invalid(
            "rmm: cannot be combined with `esphome: name_add_mac_suffix: true`. "
            "Home Assistant would prefix the RMM sensors with the suffixed device "
            f"name (sensor.{radar}_a1b2c3_target_1_x) and Radar Map Manager would "
            f"not find sensor.{radar}_presence_target_count. Disable "
            "name_add_mac_suffix or the rmm block.",
            path=[CONF_RMM],
        )

    device = _device_name()
    slug = _ha_slug(device)
    if slug != radar:
        suggested = radar.replace("_", "-")
        raise cv.Invalid(
            f"rmm.radar_name '{radar}' does not match this device's name "
            f"'{device}' (Home Assistant entity_id prefix '{slug}'). Because HA "
            f"prefixes every entity_id with the device name, RMM would see "
            f"'sensor.{slug}_target_1_x' instead. Fix by setting `esphome: name: "
            f"{suggested}` (and a friendly_name that slugifies to '{radar}', or "
            f"none), or omit radar_name to derive it from the device name.",
            path=[CONF_RMM, CONF_RADAR_NAME],
        )

    instances = full.get(DOMAIN, [])
    if isinstance(instances, dict):
        instances = [instances]
    rmm_enabled = [
        c for c in instances if c.get(CONF_RMM) and c[CONF_RMM][CONF_ENABLED]
    ]
    if len(rmm_enabled) > 1:
        raise cv.Invalid(
            "Only one ld2450_uart instance per device can enable rmm:. Home "
            "Assistant prefixes every entity with the same device name, so two "
            f"RMM radars on one ESP would both produce sensor.{radar}_target_1_x. "
            "Put the second radar on its own ESP or disable rmm: for it.",
            path=[CONF_RMM],
        )

    my_id = config[CONF_ID]
    for platform_conf in full.get("sensor", []):
        if platform_conf.get(CONF_PLATFORM) != DOMAIN:
            continue
        if not _same_id(platform_conf.get(CONF_LD2450_UART_ID), my_id):
            continue
        conflicts = [
            f"{key}.{axis}"
            for key in TARGET_KEYS
            for axis in ("x", "y")
            if key in platform_conf and axis in platform_conf[key]
        ]
        if CONF_TARGET_COUNT in platform_conf:
            conflicts.append(CONF_TARGET_COUNT)
        if conflicts:
            raise cv.Invalid(
                f"sensor: platform ld2450_uart defines {', '.join(conflicts)} for "
                f"'{getattr(my_id, 'id', my_id)}', but rmm: already generates these "
                "sensors (target_N_x/y, presence_target_count). Both would bind to "
                "the same slot and one of them would never update. Remove them "
                "from sensor: (speed, distance, angle and resolution are fine) or "
                "disable rmm:.",
                path=[CONF_RMM],
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
