import logging

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, climate, sensor, uart
from esphome.components.climate import (
    ClimateMode,
    ClimatePreset,
    ClimateSwingMode,
)
from esphome.const import (
    CONF_CUSTOM_FAN_MODES,
    CONF_ID,
    CONF_INTERNAL,
    CONF_OPTIMISTIC,
    CONF_PERIOD,
    CONF_SUPPORTED_MODES,
    CONF_SUPPORTED_PRESETS,
    CONF_SUPPORTED_SWING_MODES,
    CONF_UART_ID,
    DEVICE_CLASS_TEMPERATURE,
    ICON_THERMOMETER,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@Ra3mbl"]
DEPENDENCIES = ["climate", "uart"]
AUTO_LOAD = ["sensor", "binary_sensor"]

DEXP_AC_VERSION = "0.1.0"

CONF_CURRENT_TEMPERATURE = "current_temperature"
CONF_TURBO_STATE = "turbo_state"
CONF_IONIZER_STATE = "ionizer_state"
CONF_QUIET_STATE = "quiet_state"

dexp_ac_ns = cg.esphome_ns.namespace("dexp_ac")
DexpAirCon = dexp_ac_ns.class_(
    "DexpAirCon", climate.Climate, cg.Component, uart.UARTDevice
)
Capabilities = dexp_ac_ns.namespace("Constants")

CUSTOM_FAN_MODES = {
    "TURBO": Capabilities.TURBO,
    "QUIET": Capabilities.QUIET,
}
validate_custom_fan_modes = cv.enum(CUSTOM_FAN_MODES, upper=True)

ALLOWED_CLIMATE_MODES = {
    "HEAT_COOL": ClimateMode.CLIMATE_MODE_HEAT_COOL,
    "COOL": ClimateMode.CLIMATE_MODE_COOL,
    "HEAT": ClimateMode.CLIMATE_MODE_HEAT,
    "DRY": ClimateMode.CLIMATE_MODE_DRY,
    "FAN_ONLY": ClimateMode.CLIMATE_MODE_FAN_ONLY,
}
validate_modes = cv.enum(ALLOWED_CLIMATE_MODES, upper=True)

ALLOWED_CLIMATE_PRESETS = {
    "SLEEP": ClimatePreset.CLIMATE_PRESET_SLEEP,
}
validate_presets = cv.enum(ALLOWED_CLIMATE_PRESETS, upper=True)

ALLOWED_CLIMATE_SWING_MODES = {
    "HORIZONTAL": ClimateSwingMode.CLIMATE_SWING_HORIZONTAL,
}
validate_swing_modes = cv.enum(ALLOWED_CLIMATE_SWING_MODES, upper=True)


def output_info(config):
    _LOGGER.info("DEXP AC component version: %s", DEXP_AC_VERSION)
    return config


CONFIG_SCHEMA = cv.All(
    climate.climate_schema(DexpAirCon)
    .extend(
        {
            cv.GenerateID(): cv.declare_id(DexpAirCon),
            cv.Optional(CONF_PERIOD, default="5s"): cv.time_period,
            cv.Optional(CONF_OPTIMISTIC, default=True): cv.boolean,
            cv.Optional(CONF_CURRENT_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ).extend({cv.Optional(CONF_INTERNAL, default=True): cv.boolean}),
            cv.Optional(CONF_TURBO_STATE): binary_sensor.binary_sensor_schema(
                icon="mdi:fan-plus",
            ).extend({cv.Optional(CONF_INTERNAL, default=True): cv.boolean}),
            cv.Optional(CONF_IONIZER_STATE): binary_sensor.binary_sensor_schema(
                icon="mdi:leaf",
            ).extend({cv.Optional(CONF_INTERNAL, default=True): cv.boolean}),
            cv.Optional(CONF_QUIET_STATE): binary_sensor.binary_sensor_schema(
                icon="mdi:volume-low",
            ).extend({cv.Optional(CONF_INTERNAL, default=True): cv.boolean}),
            cv.Optional(CONF_SUPPORTED_MODES): cv.ensure_list(validate_modes),
            cv.Optional(CONF_SUPPORTED_SWING_MODES): cv.ensure_list(
                validate_swing_modes
            ),
            cv.Optional(CONF_SUPPORTED_PRESETS): cv.ensure_list(validate_presets),
            cv.Optional(CONF_CUSTOM_FAN_MODES): cv.ensure_list(
                validate_custom_fan_modes
            ),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    output_info,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_period(config[CONF_PERIOD].total_milliseconds))
    cg.add(var.set_optimistic(config[CONF_OPTIMISTIC]))

    if CONF_CURRENT_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_CURRENT_TEMPERATURE])
        cg.add(var.set_current_temperature_sensor(sens))

    if CONF_TURBO_STATE in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_TURBO_STATE])
        cg.add(var.set_turbo_sensor(sens))

    if CONF_IONIZER_STATE in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_IONIZER_STATE])
        cg.add(var.set_ionizer_sensor(sens))

    if CONF_QUIET_STATE in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_QUIET_STATE])
        cg.add(var.set_quiet_sensor(sens))

    if CONF_SUPPORTED_MODES in config:
        cg.add(var.set_supported_modes(config[CONF_SUPPORTED_MODES]))
    if CONF_SUPPORTED_SWING_MODES in config:
        cg.add(var.set_supported_swing_modes(config[CONF_SUPPORTED_SWING_MODES]))
    if CONF_SUPPORTED_PRESETS in config:
        cg.add(var.set_supported_presets(config[CONF_SUPPORTED_PRESETS]))
    if CONF_CUSTOM_FAN_MODES in config:
        cg.add(var.set_custom_fan_modes(config[CONF_CUSTOM_FAN_MODES]))
