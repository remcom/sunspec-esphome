"""Schema validation tests — run without hardware."""
import pytest
import sys as _sys
import os as _os
_sys.path.insert(0, _os.path.join(_os.path.dirname(__file__), ".."))


def test_rated_power_max():
    """rated_power must not exceed int16 max (32767)."""
    import esphome.config_validation as cv
    validator = cv.All(cv.positive_int, cv.Range(max=32767))
    assert validator(3000) == 3000
    with pytest.raises(cv.Invalid):
        validator(33000)


def test_rated_power_min():
    """rated_power must be positive."""
    import esphome.config_validation as cv
    validator = cv.All(cv.positive_int, cv.Range(max=32767))
    with pytest.raises((cv.Invalid, ValueError)):
        validator(0)


def test_manufacturer_is_string():
    """manufacturer must be a string."""
    import esphome.config_validation as cv
    assert cv.string("Solis") == "Solis"
    with pytest.raises(cv.Invalid):
        cv.string(123)


# ---- _validate_phases tests ----


def _make_base_config():
    """Minimal valid shared keys for _validate_phases tests (no sensor IDs needed)."""
    return {
        "id": "sunspec",
        "manufacturer": "Test",
        "model": "T1",
        "serial_number": "123",
        "version": "1.0",
        "rated_power": 5000,
        "ac_power": "sensor_p",
        "ac_frequency": "sensor_f",
        "temperature": "sensor_t",
    }


def test_validate_phases_1_requires_ac_voltage():
    """phases:1 without ac_voltage must raise Invalid."""
    import esphome.config_validation as cv
    from components.sunspec import _validate_phases, CONF_PHASES

    config = _make_base_config()
    config[CONF_PHASES] = 1
    with pytest.raises(cv.Invalid, match="ac_voltage"):
        _validate_phases(config)


def test_validate_phases_1_valid():
    """phases:1 with ac_voltage must pass."""
    import esphome.config_validation as cv
    from components.sunspec import _validate_phases, CONF_PHASES, CONF_AC_VOLTAGE

    config = _make_base_config()
    config[CONF_PHASES] = 1
    config[CONF_AC_VOLTAGE] = "sensor_v"
    result = _validate_phases(config)
    assert result[CONF_AC_VOLTAGE] == "sensor_v"


def test_validate_phases_1_rejects_ac_voltage_a():
    """phases:1 with ac_voltage_a must raise Invalid."""
    import esphome.config_validation as cv
    from components.sunspec import _validate_phases, CONF_PHASES, CONF_AC_VOLTAGE, CONF_AC_VOLTAGE_A

    config = _make_base_config()
    config[CONF_PHASES] = 1
    config[CONF_AC_VOLTAGE] = "sensor_v"
    config[CONF_AC_VOLTAGE_A] = "sensor_va"
    with pytest.raises(cv.Invalid, match="ac_voltage_a"):
        _validate_phases(config)


def test_validate_phases_3_valid_no_current():
    """phases:3 with all three voltages and no currents must pass."""
    import esphome.config_validation as cv
    from components.sunspec import (
        _validate_phases, CONF_PHASES,
        CONF_AC_VOLTAGE_A, CONF_AC_VOLTAGE_B, CONF_AC_VOLTAGE_C,
    )

    config = _make_base_config()
    config[CONF_PHASES] = 3
    config[CONF_AC_VOLTAGE_A] = "sensor_va"
    config[CONF_AC_VOLTAGE_B] = "sensor_vb"
    config[CONF_AC_VOLTAGE_C] = "sensor_vc"
    result = _validate_phases(config)
    assert result[CONF_AC_VOLTAGE_A] == "sensor_va"


def test_validate_phases_3_valid_with_currents():
    """phases:3 with all three voltages and all three currents must pass."""
    import esphome.config_validation as cv
    from components.sunspec import (
        _validate_phases, CONF_PHASES,
        CONF_AC_VOLTAGE_A, CONF_AC_VOLTAGE_B, CONF_AC_VOLTAGE_C,
        CONF_AC_CURRENT_A, CONF_AC_CURRENT_B, CONF_AC_CURRENT_C,
    )

    config = _make_base_config()
    config[CONF_PHASES] = 3
    config[CONF_AC_VOLTAGE_A] = "sensor_va"
    config[CONF_AC_VOLTAGE_B] = "sensor_vb"
    config[CONF_AC_VOLTAGE_C] = "sensor_vc"
    config[CONF_AC_CURRENT_A] = "sensor_ia"
    config[CONF_AC_CURRENT_B] = "sensor_ib"
    config[CONF_AC_CURRENT_C] = "sensor_ic"
    result = _validate_phases(config)
    assert result[CONF_AC_CURRENT_A] == "sensor_ia"


def test_validate_phases_3_missing_voltage_b():
    """phases:3 missing ac_voltage_b must raise Invalid."""
    import esphome.config_validation as cv
    from components.sunspec import (
        _validate_phases, CONF_PHASES,
        CONF_AC_VOLTAGE_A, CONF_AC_VOLTAGE_C,
    )

    config = _make_base_config()
    config[CONF_PHASES] = 3
    config[CONF_AC_VOLTAGE_A] = "sensor_va"
    config[CONF_AC_VOLTAGE_C] = "sensor_vc"
    with pytest.raises(cv.Invalid, match="ac_voltage_b"):
        _validate_phases(config)


def test_validate_phases_3_rejects_ac_voltage():
    """phases:3 with ac_voltage must raise Invalid."""
    import esphome.config_validation as cv
    from components.sunspec import (
        _validate_phases, CONF_PHASES, CONF_AC_VOLTAGE,
        CONF_AC_VOLTAGE_A, CONF_AC_VOLTAGE_B, CONF_AC_VOLTAGE_C,
    )

    config = _make_base_config()
    config[CONF_PHASES] = 3
    config[CONF_AC_VOLTAGE] = "sensor_v"
    config[CONF_AC_VOLTAGE_A] = "sensor_va"
    config[CONF_AC_VOLTAGE_B] = "sensor_vb"
    config[CONF_AC_VOLTAGE_C] = "sensor_vc"
    with pytest.raises(cv.Invalid, match="ac_voltage"):
        _validate_phases(config)


def test_validate_phases_3_rejects_ac_current():
    """phases:3 with ac_current (single-phase key) must raise Invalid."""
    import esphome.config_validation as cv
    from components.sunspec import (
        _validate_phases, CONF_PHASES, CONF_AC_CURRENT,
        CONF_AC_VOLTAGE_A, CONF_AC_VOLTAGE_B, CONF_AC_VOLTAGE_C,
    )

    config = _make_base_config()
    config[CONF_PHASES] = 3
    config[CONF_AC_VOLTAGE_A] = "sensor_va"
    config[CONF_AC_VOLTAGE_B] = "sensor_vb"
    config[CONF_AC_VOLTAGE_C] = "sensor_vc"
    config[CONF_AC_CURRENT] = "sensor_i"
    with pytest.raises(cv.Invalid, match="ac_current"):
        _validate_phases(config)


def test_validate_phases_3_partial_currents_rejected():
    """phases:3 with only ac_current_a (partial set) must raise Invalid."""
    import esphome.config_validation as cv
    from components.sunspec import (
        _validate_phases, CONF_PHASES,
        CONF_AC_VOLTAGE_A, CONF_AC_VOLTAGE_B, CONF_AC_VOLTAGE_C,
        CONF_AC_CURRENT_A,
    )

    config = _make_base_config()
    config[CONF_PHASES] = 3
    config[CONF_AC_VOLTAGE_A] = "sensor_va"
    config[CONF_AC_VOLTAGE_B] = "sensor_vb"
    config[CONF_AC_VOLTAGE_C] = "sensor_vc"
    config[CONF_AC_CURRENT_A] = "sensor_ia"
    with pytest.raises(cv.Invalid, match="ac_current"):
        _validate_phases(config)
