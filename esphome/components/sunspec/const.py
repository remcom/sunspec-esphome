"""Constants for SunSpec component."""

# Configuration keys
CONF_MAX_CONNECTIONS = "max_connections"
CONF_BASE_ADDRESS = "base_address"
CONF_MANUFACTURER = "manufacturer"
CONF_SERIAL_NUMBER = "serial_number"
CONF_INVERTER_SINGLE_PHASE = "inverter_single_phase"
CONF_INVERTER_THREE_PHASE = "inverter_three_phase"
CONF_STALE_TIMEOUT = "stale_timeout"
CONF_CONTROLS = "controls"
CONF_ON_POWER_LIMIT = "on_power_limit"

# Model 101/103 - Inverter sensor mappings
CONF_AC_POWER = "ac_power"
CONF_AC_VOLTAGE = "ac_voltage"
CONF_AC_CURRENT = "ac_current"
CONF_AC_FREQUENCY = "ac_frequency"
CONF_DC_POWER = "dc_power"
CONF_DC_VOLTAGE = "dc_voltage"
CONF_DC_CURRENT = "dc_current"
CONF_TEMPERATURE = "temperature"
CONF_ENERGY = "energy"

# Model 103 - per-phase sensor mappings
CONF_AC_CURRENT_PHASE_A = "ac_current_phase_a"
CONF_AC_CURRENT_PHASE_B = "ac_current_phase_b"
CONF_AC_CURRENT_PHASE_C = "ac_current_phase_c"
CONF_AC_VOLTAGE_PHASE_A = "ac_voltage_phase_a"
CONF_AC_VOLTAGE_PHASE_B = "ac_voltage_phase_b"
CONF_AC_VOLTAGE_PHASE_C = "ac_voltage_phase_c"
