"""Support for SunSpec protocol for PV inverter emulation."""
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import automation
from esphome.components import sensor
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_MODEL,
    CONF_PORT,
    CONF_TRIGGER_ID,
    CONF_VERSION,
)
from esphome.core import CORE
from .const import (
    CONF_AC_CURRENT,
    CONF_AC_CURRENT_PHASE_A,
    CONF_AC_CURRENT_PHASE_B,
    CONF_AC_CURRENT_PHASE_C,
    CONF_AC_FREQUENCY,
    CONF_AC_POWER,
    CONF_AC_VOLTAGE,
    CONF_AC_VOLTAGE_PHASE_A,
    CONF_AC_VOLTAGE_PHASE_B,
    CONF_AC_VOLTAGE_PHASE_C,
    CONF_BASE_ADDRESS,
    CONF_CONTROLS,
    CONF_DC_CURRENT,
    CONF_DC_POWER,
    CONF_DC_VOLTAGE,
    CONF_ENERGY,
    CONF_INVERTER_SINGLE_PHASE,
    CONF_INVERTER_THREE_PHASE,
    CONF_MANUFACTURER,
    CONF_MAX_CONNECTIONS,
    CONF_ON_POWER_LIMIT,
    CONF_SERIAL_NUMBER,
    CONF_STALE_TIMEOUT,
    CONF_TEMPERATURE,
)

CODEOWNERS = ["@remcom"]
DEPENDENCIES = ["network"]
MULTI_CONF = True

sunspec_ns = cg.esphome_ns.namespace("sunspec")
SunSpecServer = sunspec_ns.class_("SunSpecServer", cg.Component, cg.Controller)
PowerLimitTrigger = sunspec_ns.class_(
    "PowerLimitTrigger", automation.Trigger.template(cg.float_, cg.bool_)
)

# Sensors shared by Model 101 (single phase) and Model 103 (three phase)
BASE_SENSOR_SETTERS = {
    CONF_AC_POWER: "set_ac_power_sensor",
    CONF_AC_CURRENT: "set_ac_current_sensor",
    CONF_AC_FREQUENCY: "set_ac_frequency_sensor",
    CONF_DC_POWER: "set_dc_power_sensor",
    CONF_DC_VOLTAGE: "set_dc_voltage_sensor",
    CONF_DC_CURRENT: "set_dc_current_sensor",
    CONF_TEMPERATURE: "set_temperature_sensor",
    CONF_ENERGY: "set_energy_sensor",
}

SINGLE_PHASE_SETTERS = {
    **BASE_SENSOR_SETTERS,
    CONF_AC_VOLTAGE: "set_ac_voltage_sensor",
}

THREE_PHASE_SETTERS = {
    **BASE_SENSOR_SETTERS,
    CONF_AC_CURRENT_PHASE_A: "set_ac_current_phase_a_sensor",
    CONF_AC_CURRENT_PHASE_B: "set_ac_current_phase_b_sensor",
    CONF_AC_CURRENT_PHASE_C: "set_ac_current_phase_c_sensor",
    CONF_AC_VOLTAGE_PHASE_A: "set_ac_voltage_phase_a_sensor",
    CONF_AC_VOLTAGE_PHASE_B: "set_ac_voltage_phase_b_sensor",
    CONF_AC_VOLTAGE_PHASE_C: "set_ac_voltage_phase_c_sensor",
}


def _sensor_schema(setters):
    return cv.Schema({cv.Optional(key): cv.use_id(sensor.Sensor) for key in setters})


CONTROLS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ON_POWER_LIMIT): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PowerLimitTrigger),
            }
        ),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SunSpecServer),
            cv.Optional(CONF_PORT, default=502): cv.port,
            cv.Optional(CONF_MAX_CONNECTIONS, default=4): cv.int_range(min=1, max=8),
            cv.Optional(CONF_BASE_ADDRESS, default=40000): cv.uint16_t,
            cv.Optional(CONF_ADDRESS, default=1): cv.int_range(min=1, max=247),
            cv.Optional(
                CONF_STALE_TIMEOUT, default="5min"
            ): cv.positive_time_period_milliseconds,
            cv.Required(CONF_MANUFACTURER): cv.string_strict,
            cv.Required(CONF_MODEL): cv.string_strict,
            cv.Optional(CONF_SERIAL_NUMBER): cv.string_strict,
            cv.Optional(CONF_VERSION, default="1.0"): cv.string_strict,
            cv.Optional(CONF_INVERTER_SINGLE_PHASE): _sensor_schema(SINGLE_PHASE_SETTERS),
            cv.Optional(CONF_INVERTER_THREE_PHASE): _sensor_schema(THREE_PHASE_SETTERS),
            cv.Optional(CONF_CONTROLS): CONTROLS_SCHEMA,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.has_at_most_one_key(CONF_INVERTER_SINGLE_PHASE, CONF_INVERTER_THREE_PHASE),
    cv.only_on_esp32,
)


def _final_validate(config):
    full = fv.full_config.get()
    ports = [conf[CONF_PORT] for conf in full.get("sunspec", [])]
    if ports.count(config[CONF_PORT]) > 1:
        raise cv.Invalid(
            f"Multiple sunspec servers are configured on port {config[CONF_PORT]}; "
            "each server needs a unique port"
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def _register_sensors(var, conf, setters):
    for key, setter in setters.items():
        if key in conf:
            sens = await cg.get_variable(conf[key])
            cg.add(getattr(var, setter)(sens))


async def to_code(config):
    CORE.register_controller()

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Configure TCP server settings
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_max_connections(config[CONF_MAX_CONNECTIONS]))
    cg.add(var.set_base_address(config[CONF_BASE_ADDRESS]))
    cg.add(var.set_unit_address(config[CONF_ADDRESS]))
    cg.add(var.set_stale_timeout(config[CONF_STALE_TIMEOUT].total_milliseconds))

    # Model 1 - Common
    cg.add(var.set_manufacturer(config[CONF_MANUFACTURER]))
    cg.add(var.set_model(config[CONF_MODEL]))
    if CONF_SERIAL_NUMBER in config:
        cg.add(var.set_serial_number(config[CONF_SERIAL_NUMBER]))
    cg.add(var.set_version(config[CONF_VERSION]))

    # Model 101 (single phase) or Model 103 (three phase)
    if CONF_INVERTER_SINGLE_PHASE in config:
        await _register_sensors(var, config[CONF_INVERTER_SINGLE_PHASE], SINGLE_PHASE_SETTERS)
    elif CONF_INVERTER_THREE_PHASE in config:
        cg.add(var.set_model_id(103))
        await _register_sensors(var, config[CONF_INVERTER_THREE_PHASE], THREE_PHASE_SETTERS)

    # Model 123 - Immediate Controls
    if CONF_CONTROLS in config:
        cg.add(var.set_controls_enabled(True))
        for conf in config[CONF_CONTROLS].get(CONF_ON_POWER_LIMIT, []):
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
            await automation.build_automation(
                trigger, [(cg.float_, "level"), (cg.bool_, "enabled")], conf
            )
