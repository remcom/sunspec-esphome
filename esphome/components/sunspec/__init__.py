"""Support for SunSpec protocol for PV inverter emulation."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, modbus_tcp
from esphome.const import CONF_ID, CONF_MODEL, CONF_VERSION, CONF_PORT
from .const import (
    CONF_AC_CURRENT,
    CONF_AC_FREQUENCY,
    CONF_AC_POWER,
    CONF_AC_VOLTAGE,
    CONF_BASE_ADDRESS,
    CONF_DC_CURRENT,
    CONF_DC_POWER,
    CONF_DC_VOLTAGE,
    CONF_INVERTER_SINGLE_PHASE,
    CONF_MANUFACTURER,
    CONF_MAX_CONNECTIONS,
    CONF_MODBUS_TCP_ID,
    CONF_SERIAL_NUMBER,
    CONF_TEMPERATURE,
)

AUTO_LOAD = ["modbus_tcp"]
CODEOWNERS = ["@esphome/core"]

sunspec_ns = cg.esphome_ns.namespace("sunspec")
SunSpecServer = sunspec_ns.class_("SunSpecServer", cg.Component, cg.Controller)

# Inverter sensor configuration schema - all sensors are optional
INVERTER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_AC_POWER): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_AC_VOLTAGE): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_AC_CURRENT): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_AC_FREQUENCY): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_DC_POWER): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_DC_VOLTAGE): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_DC_CURRENT): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_TEMPERATURE): cv.use_id(sensor.Sensor),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.ensure_list(
        cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(SunSpecServer),
                cv.Optional(CONF_MODBUS_TCP_ID): cv.use_id(modbus_tcp.ModbusTCP),
                cv.Optional(CONF_PORT, default=502): cv.port,
                cv.Optional(CONF_MAX_CONNECTIONS, default=4): cv.int_range(min=1, max=8),
                cv.Optional(CONF_BASE_ADDRESS, default=40000): cv.uint16_t,
                cv.Required(CONF_MANUFACTURER): cv.string_strict,
                cv.Required(CONF_MODEL): cv.string_strict,
                cv.Optional(CONF_SERIAL_NUMBER): cv.string_strict,
                cv.Optional(CONF_VERSION, default="1.0"): cv.string_strict,
                cv.Optional(CONF_INVERTER_SINGLE_PHASE): INVERTER_SCHEMA,
            }
        ).extend(cv.COMPONENT_SCHEMA)
    )
)


async def to_code(config):
    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)

        # Link to Modbus TCP transport (external or create internal)
        if CONF_MODBUS_TCP_ID in conf:
            # Use external ModbusTCP instance
            tcp = await cg.get_variable(conf[CONF_MODBUS_TCP_ID])
            cg.add(var.set_modbus_tcp(tcp))
        else:
            # Create internal ModbusTCP instance
            cg.add(var.set_owns_modbus_tcp(True))
            tcp = cg.variable(
                cg.RawExpression("auto *"),
                cg.RawExpression(f"{var}->get_internal_modbus_tcp()"),
            )
            cg.add(tcp.set_port(conf[CONF_PORT]))
            cg.add(tcp.set_max_connections(conf[CONF_MAX_CONNECTIONS]))
            await cg.register_component(tcp, conf)

        cg.add(var.set_base_address(conf[CONF_BASE_ADDRESS]))

        # Model 1 - Common
        cg.add(var.set_manufacturer(conf[CONF_MANUFACTURER]))
        cg.add(var.set_model(conf[CONF_MODEL]))
        if CONF_SERIAL_NUMBER in conf:
            cg.add(var.set_serial_number(conf[CONF_SERIAL_NUMBER]))
        cg.add(var.set_version(conf[CONF_VERSION]))

        # Model 101 - Inverter (optional sensors)
        if CONF_INVERTER_SINGLE_PHASE in conf:
            inv = conf[CONF_INVERTER_SINGLE_PHASE]

            if CONF_AC_POWER in inv:
                sens = await cg.get_variable(inv[CONF_AC_POWER])
                cg.add(var.set_ac_power_sensor(sens))

            if CONF_AC_VOLTAGE in inv:
                sens = await cg.get_variable(inv[CONF_AC_VOLTAGE])
                cg.add(var.set_ac_voltage_sensor(sens))

            if CONF_AC_CURRENT in inv:
                sens = await cg.get_variable(inv[CONF_AC_CURRENT])
                cg.add(var.set_ac_current_sensor(sens))

            if CONF_AC_FREQUENCY in inv:
                sens = await cg.get_variable(inv[CONF_AC_FREQUENCY])
                cg.add(var.set_ac_frequency_sensor(sens))

            if CONF_DC_POWER in inv:
                sens = await cg.get_variable(inv[CONF_DC_POWER])
                cg.add(var.set_dc_power_sensor(sens))

            if CONF_DC_VOLTAGE in inv:
                sens = await cg.get_variable(inv[CONF_DC_VOLTAGE])
                cg.add(var.set_dc_voltage_sensor(sens))

            if CONF_DC_CURRENT in inv:
                sens = await cg.get_variable(inv[CONF_DC_CURRENT])
                cg.add(var.set_dc_current_sensor(sens))

            if CONF_TEMPERATURE in inv:
                sens = await cg.get_variable(inv[CONF_TEMPERATURE])
                cg.add(var.set_temperature_sensor(sens))
