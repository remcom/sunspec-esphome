import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, modbus_controller, number
from esphome.const import CONF_ID, CONF_VERSION

DEPENDENCIES = ["sensor"]

sunspec_ns = cg.esphome_ns.namespace("sunspec")
SunspecComponent = sunspec_ns.class_("SunspecComponent", cg.Component)

CONF_MANUFACTURER = "manufacturer"
CONF_MODEL_NAME = "model"
CONF_SERIAL_NUMBER = "serial_number"
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
CONF_PHASES = "phases"
CONF_AC_VOLTAGE_A = "ac_voltage_a"
CONF_AC_VOLTAGE_B = "ac_voltage_b"
CONF_AC_VOLTAGE_C = "ac_voltage_c"
CONF_AC_CURRENT_A = "ac_current_a"
CONF_AC_CURRENT_B = "ac_current_b"
CONF_AC_CURRENT_C = "ac_current_c"


def _validate_power_limit(config):
    has_register = CONF_POWER_LIMIT_REGISTER in config
    has_controller = CONF_MODBUS_CONTROLLER_ID in config
    if has_register and not has_controller:
        raise cv.Invalid(
            f"'{CONF_MODBUS_CONTROLLER_ID}' is required when '{CONF_POWER_LIMIT_REGISTER}' is set"
        )
    if has_controller and not has_register:
        raise cv.Invalid(
            f"'{CONF_POWER_LIMIT_REGISTER}' is required when '{CONF_MODBUS_CONTROLLER_ID}' is set"
        )
    return config


def _validate_phases(config):
    phases = config.get(CONF_PHASES, 1)
    if phases == 1:
        if CONF_AC_VOLTAGE not in config:
            raise cv.Invalid(
                f"'{CONF_AC_VOLTAGE}' is required when phases: 1"
            )
        for key in (CONF_AC_VOLTAGE_A, CONF_AC_VOLTAGE_B, CONF_AC_VOLTAGE_C,
                    CONF_AC_CURRENT_A, CONF_AC_CURRENT_B, CONF_AC_CURRENT_C):
            if key in config:
                raise cv.Invalid(
                    f"'{key}' is not allowed when phases: 1"
                )
    else:  # phases == 3
        if CONF_AC_VOLTAGE in config:
            raise cv.Invalid(
                f"'{CONF_AC_VOLTAGE}' is not allowed when phases: 3; use ac_voltage_a"
            )
        if CONF_AC_CURRENT in config:
            raise cv.Invalid(
                f"'{CONF_AC_CURRENT}' is not allowed when phases: 3; use ac_current_a/b/c"
            )
        for key in (CONF_AC_VOLTAGE_A, CONF_AC_VOLTAGE_B, CONF_AC_VOLTAGE_C):
            if key not in config:
                raise cv.Invalid(
                    f"'{key}' is required when phases: 3"
                )
        present = [k for k in (CONF_AC_CURRENT_A, CONF_AC_CURRENT_B, CONF_AC_CURRENT_C)
                   if k in config]
        if present and len(present) != 3:
            raise cv.Invalid(
                "ac_current_a, ac_current_b, and ac_current_c must all be configured or none"
            )
    return config


CONFIG_SCHEMA = cv.All(
    cv.only_on_esp32,
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SunspecComponent),
            cv.Required(CONF_MANUFACTURER): cv.All(cv.string, cv.Length(max=32)),
            cv.Required(CONF_MODEL_NAME): cv.All(cv.string, cv.Length(max=32)),
            cv.Required(CONF_SERIAL_NUMBER): cv.All(cv.string, cv.Length(max=32)),
            cv.Required(CONF_VERSION): cv.All(cv.string, cv.Length(max=16)),
            cv.Required(CONF_RATED_POWER): cv.All(cv.positive_int, cv.Range(max=32767)),
            cv.Optional(CONF_PHASES, default=1): cv.one_of(1, 3, int=True),
            # 1-phase sensors
            cv.Optional(CONF_AC_VOLTAGE): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_AC_CURRENT): cv.use_id(sensor.Sensor),
            # 3-phase sensors
            cv.Optional(CONF_AC_VOLTAGE_A): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_AC_VOLTAGE_B): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_AC_VOLTAGE_C): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_AC_CURRENT_A): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_AC_CURRENT_B): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_AC_CURRENT_C): cv.use_id(sensor.Sensor),
            # shared sensors
            cv.Required(CONF_AC_POWER): cv.use_id(sensor.Sensor),
            cv.Required(CONF_AC_FREQUENCY): cv.use_id(sensor.Sensor),
            cv.Required(CONF_TEMPERATURE): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_ENERGY_TOTAL): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_MODBUS_CONTROLLER_ID): cv.use_id(
                modbus_controller.ModbusController
            ),
            cv.Optional(CONF_POWER_LIMIT_REGISTER): cv.positive_int,
            cv.Optional(CONF_POWER_LIMIT_NUMBER_ID): cv.use_id(number.Number),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_power_limit,
    _validate_phases,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_manufacturer(config[CONF_MANUFACTURER]))
    cg.add(var.set_model(config[CONF_MODEL_NAME]))
    cg.add(var.set_serial_number(config[CONF_SERIAL_NUMBER]))
    cg.add(var.set_version(config[CONF_VERSION]))
    cg.add(var.set_rated_power(config[CONF_RATED_POWER]))

    three_phase = config[CONF_PHASES] == 3
    cg.add(var.set_three_phase(three_phase))

    if three_phase:
        for conf_key, setter in [
            (CONF_AC_VOLTAGE_A, "set_ac_voltage"),
            (CONF_AC_VOLTAGE_B, "set_ac_voltage_b"),
            (CONF_AC_VOLTAGE_C, "set_ac_voltage_c"),
        ]:
            sens = await cg.get_variable(config[conf_key])
            cg.add(getattr(var, setter)(sens))
        if CONF_AC_CURRENT_A in config:
            for conf_key, setter in [
                (CONF_AC_CURRENT_A, "set_ac_current_a"),
                (CONF_AC_CURRENT_B, "set_ac_current_b"),
                (CONF_AC_CURRENT_C, "set_ac_current_c"),
            ]:
                sens = await cg.get_variable(config[conf_key])
                cg.add(getattr(var, setter)(sens))
    else:
        sens = await cg.get_variable(config[CONF_AC_VOLTAGE])
        cg.add(var.set_ac_voltage(sens))
        if CONF_AC_CURRENT in config:
            sens = await cg.get_variable(config[CONF_AC_CURRENT])
            cg.add(var.set_ac_current(sens))

    for conf_key, setter in [
        (CONF_AC_POWER, "set_ac_power"),
        (CONF_AC_FREQUENCY, "set_ac_frequency"),
        (CONF_TEMPERATURE, "set_temperature"),
    ]:
        sens = await cg.get_variable(config[conf_key])
        cg.add(getattr(var, setter)(sens))

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
