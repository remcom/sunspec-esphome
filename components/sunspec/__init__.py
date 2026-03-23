import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, modbus_controller, number
from esphome.const import CONF_ID

sunspec_ns = cg.esphome_ns.namespace("sunspec")
SunspecComponent = sunspec_ns.class_("SunspecComponent", cg.Component)

CONF_MANUFACTURER = "manufacturer"
CONF_MODEL_NAME = "model"
CONF_SERIAL_NUMBER = "serial_number"
CONF_VERSION = "version"
CONF_RATED_POWER = "rated_power"
CONF_AC_POWER = "ac_power"
CONF_AC_VOLTAGE = "ac_voltage"
CONF_AC_CURRENT = "ac_current"
CONF_AC_FREQUENCY = "ac_frequency"
CONF_TEMPERATURE = "temperature"
CONF_ENERGY_TOTAL = "energy_total"
CONF_MODBUS_CONTROLLER_ID = "modbus_controller_id"
CONF_POWER_LIMIT_REGISTER = "power_limit_register"
CONF_POWER_LIMIT_NUMBER_ID = "power_limit_number_id"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SunspecComponent),
        cv.Required(CONF_MANUFACTURER): cv.string,
        cv.Required(CONF_MODEL_NAME): cv.string,
        cv.Required(CONF_SERIAL_NUMBER): cv.string,
        cv.Required(CONF_VERSION): cv.string,
        cv.Required(CONF_RATED_POWER): cv.All(cv.positive_int, cv.Range(max=32767)),
        cv.Required(CONF_AC_POWER): cv.use_id(sensor.Sensor),
        cv.Required(CONF_AC_VOLTAGE): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_AC_CURRENT): cv.use_id(sensor.Sensor),
        cv.Required(CONF_AC_FREQUENCY): cv.use_id(sensor.Sensor),
        cv.Required(CONF_TEMPERATURE): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_ENERGY_TOTAL): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_MODBUS_CONTROLLER_ID): cv.use_id(
            modbus_controller.ModbusController
        ),
        cv.Optional(CONF_POWER_LIMIT_REGISTER): cv.positive_int,
        cv.Optional(CONF_POWER_LIMIT_NUMBER_ID): cv.use_id(number.Number),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_manufacturer(config[CONF_MANUFACTURER]))
    cg.add(var.set_model(config[CONF_MODEL_NAME]))
    cg.add(var.set_serial_number(config[CONF_SERIAL_NUMBER]))
    cg.add(var.set_version(config[CONF_VERSION]))
    cg.add(var.set_rated_power(config[CONF_RATED_POWER]))

    for conf_key, setter in [
        (CONF_AC_POWER, "set_ac_power"),
        (CONF_AC_VOLTAGE, "set_ac_voltage"),
        (CONF_AC_FREQUENCY, "set_ac_frequency"),
        (CONF_TEMPERATURE, "set_temperature"),
    ]:
        sens = await cg.get_variable(config[conf_key])
        cg.add(getattr(var, setter)(sens))

    if CONF_AC_CURRENT in config:
        sens = await cg.get_variable(config[CONF_AC_CURRENT])
        cg.add(var.set_ac_current(sens))

    if CONF_ENERGY_TOTAL in config:
        sens = await cg.get_variable(config[CONF_ENERGY_TOTAL])
        cg.add(var.set_energy_total(sens))

    if CONF_MODBUS_CONTROLLER_ID in config:
        ctrl = await cg.get_variable(config[CONF_MODBUS_CONTROLLER_ID])
        cg.add(var.set_modbus_controller(ctrl))

    if CONF_POWER_LIMIT_REGISTER in config:
        cg.add(var.set_power_limit_register(config[CONF_POWER_LIMIT_REGISTER]))

    if CONF_POWER_LIMIT_NUMBER_ID in config:
        num = await cg.get_variable(config[CONF_POWER_LIMIT_NUMBER_ID])
        cg.add(var.set_power_limit_number(num))
