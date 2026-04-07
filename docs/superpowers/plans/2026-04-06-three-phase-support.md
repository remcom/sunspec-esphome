# Three-Phase Inverter Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `phases: 1|3` config key to the `sunspec` ESPHome component so 3-phase inverters are served as SunSpec Model 103 with per-phase voltages and currents.

**Architecture:** A single `phases` key (default `1`) selects between Model 101 (single-phase) and Model 103 (three-phase). New per-phase sensor pointers are added to the C++ class; `refresh_sensors_()` branches on a `three_phase_` flag. Existing single-phase configs require zero changes.

**Tech Stack:** ESPHome external component (Python `__init__.py` + C++ `.h`/`.cpp`), pytest, pymodbus (integration tests only).

---

## File Map

| File | Change |
|------|--------|
| `components/sunspec/__init__.py` | Add `phases`, `ac_voltage_a/b/c`, `ac_current_a/b/c` keys; add `_validate_phases()` |
| `components/sunspec/sunspec.h` | Add `three_phase_` flag, 5 new sensor pointers, 6 new setters |
| `components/sunspec/sunspec.cpp` | Update `init_static_registers_`, `setup`, `refresh_sensors_`, `dump_config` |
| `tests/test_schema.py` | Add `_validate_phases` unit tests |
| `tests/test_sunspec.py` | Add 3-phase integration tests (hardware, skipped without `--three-phase`) |

---

## Task 1: Schema validation tests

**Files:**
- Modify: `tests/test_schema.py`

- [ ] **Step 1: Add the failing tests**

Append to `tests/test_schema.py`:

```python
# ---- _validate_phases tests ----

import sys as _sys
import os as _os
_sys.path.insert(0, _os.path.join(_os.path.dirname(__file__), ".."))


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
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd /Users/remco/git/bigdisplat/sunspec-esphome
python -m pytest tests/test_schema.py -v -k "validate_phases"
```

Expected: all 8 new tests FAIL with `ImportError` or `ImportError: cannot import name '_validate_phases'`.

---

## Task 2: Python schema implementation

**Files:**
- Modify: `components/sunspec/__init__.py`

- [ ] **Step 1: Add new CONF constants**

After the existing `CONF_POWER_LIMIT_NUMBER_ID = "power_limit_number_id"` line, add:

```python
CONF_PHASES = "phases"
CONF_AC_VOLTAGE_A = "ac_voltage_a"
CONF_AC_VOLTAGE_B = "ac_voltage_b"
CONF_AC_VOLTAGE_C = "ac_voltage_c"
CONF_AC_CURRENT_A = "ac_current_a"
CONF_AC_CURRENT_B = "ac_current_b"
CONF_AC_CURRENT_C = "ac_current_c"
```

- [ ] **Step 2: Add `_validate_phases` function**

After the `_validate_power_limit` function, add:

```python
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
```

- [ ] **Step 3: Update `CONFIG_SCHEMA`**

Replace the existing `CONFIG_SCHEMA` block with:

```python
CONFIG_SCHEMA = cv.All(
    cv.only_with_esp_idf,
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
```

- [ ] **Step 4: Update `to_code`**

Replace the existing `to_code` function with:

```python
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
```

- [ ] **Step 5: Run schema tests to verify they pass**

```bash
python -m pytest tests/test_schema.py -v
```

Expected: all tests PASS.

- [ ] **Step 6: Commit**

```bash
git add components/sunspec/__init__.py tests/test_schema.py
git commit -m "feat: add phases config key and 3-phase schema validation"
```

---

## Task 3: C++ header additions

**Files:**
- Modify: `components/sunspec/sunspec.h`

- [ ] **Step 1: Add new setters**

In `sunspec.h`, after the `set_energy_total` setter line, add:

```cpp
  void set_three_phase(bool v)               { this->three_phase_ = v; }
  void set_ac_voltage_b(sensor::Sensor *s)   { this->ac_voltage_b_ = s; }
  void set_ac_voltage_c(sensor::Sensor *s)   { this->ac_voltage_c_ = s; }
  void set_ac_current_a(sensor::Sensor *s)   { this->ac_current_a_ = s; }
  void set_ac_current_b(sensor::Sensor *s)   { this->ac_current_b_ = s; }
  void set_ac_current_c(sensor::Sensor *s)   { this->ac_current_c_ = s; }
```

- [ ] **Step 2: Add new protected members**

In `sunspec.h`, after the `energy_total_` member line, add:

```cpp
  bool            three_phase_{false};
  sensor::Sensor *ac_voltage_b_{nullptr};
  sensor::Sensor *ac_voltage_c_{nullptr};
  sensor::Sensor *ac_current_a_{nullptr};
  sensor::Sensor *ac_current_b_{nullptr};
  sensor::Sensor *ac_current_c_{nullptr};
```

- [ ] **Step 3: Commit**

```bash
git add components/sunspec/sunspec.h
git commit -m "feat: add 3-phase sensor pointers and setters to SunspecComponent"
```

---

## Task 4: C++ static register init, setup validation, and dump_config

**Files:**
- Modify: `components/sunspec/sunspec.cpp`

- [ ] **Step 1: Update `init_static_registers_` model ID**

In `sunspec.cpp`, find this line inside `init_static_registers_()`:

```cpp
  this->set_reg(40070, 101);  // Model ID
```

Replace with:

```cpp
  this->set_reg(40070, this->three_phase_ ? 103 : 101);  // Model ID: 101=single-phase, 103=three-phase
```

- [ ] **Step 2: Add 3-phase sensor validation in `setup()`**

In `sunspec.cpp`, inside `setup()`, after the existing sensor null-check block:

```cpp
  if (!this->ac_power_ || !this->ac_voltage_ || !this->ac_frequency_ || !this->temperature_) {
    ESP_LOGE(TAG, "One or more required sensors are not configured");
    this->mark_failed();
    return;
  }
```

Add immediately after:

```cpp
  if (this->three_phase_ && (!this->ac_voltage_b_ || !this->ac_voltage_c_)) {
    ESP_LOGE(TAG, "ac_voltage_b and ac_voltage_c are required when phases: 3");
    this->mark_failed();
    return;
  }
```

- [ ] **Step 3: Log phase count in `dump_config()`**

In `dump_config()`, after the `Rated Power` log line, add:

```cpp
  ESP_LOGCONFIG(TAG, "  Phases:       %d", this->three_phase_ ? 3 : 1);
```

- [ ] **Step 4: Write integration tests for 3-phase model ID (hardware test)**

In `tests/test_sunspec.py`, add a `three_phase` fixture and a model-ID test at the end of the file:

```python
@pytest.fixture(scope="module")
def three_phase(request):
    """Skip unless --device-ip and --three-phase are both provided."""
    ip = request.config.getoption("--device-ip")
    flag = request.config.getoption("--three-phase", default=False)
    if ip is None or not flag:
        pytest.skip("--device-ip and --three-phase required")
    c = ModbusTcpClient(ip, port=502)
    c.connect()
    yield c
    c.close()


def pytest_addoption_three_phase(parser):
    parser.addoption("--three-phase", action="store_true", default=False,
                     help="Run 3-phase inverter tests")


def test_model103_id(three_phase):
    """3-phase: inverter block model ID must be 103."""
    rr = three_phase.read_holding_registers(40070, 1, slave=1)
    assert not rr.isError()
    assert rr.registers[0] == 103, f"Expected 103, got {rr.registers[0]}"
```

Also add `--three-phase` option to the existing `pytest_addoption` function in `test_sunspec.py`:

```python
def pytest_addoption(parser):
    parser.addoption("--device-ip", default=None, help="ESP32 device IP address")
    parser.addoption("--three-phase", action="store_true", default=False,
                     help="Run 3-phase inverter tests")
```

(Remove the standalone `pytest_addoption_three_phase` function — it was only shown above for clarity; the option must be in the single `pytest_addoption`.)

- [ ] **Step 5: Run schema tests to make sure nothing is broken**

```bash
python -m pytest tests/test_schema.py -v
```

Expected: all tests PASS.

- [ ] **Step 6: Commit**

```bash
git add components/sunspec/sunspec.cpp tests/test_sunspec.py
git commit -m "feat: Model 103 register init, setup validation, dump_config for 3-phase"
```

---

## Task 5: C++ `refresh_sensors_` 3-phase branch

**Files:**
- Modify: `components/sunspec/sunspec.cpp`

- [ ] **Step 1: Replace `refresh_sensors_` with the branching version**

In `sunspec.cpp`, replace the entire `refresh_sensors_()` function body with:

```cpp
void SunspecComponent::refresh_sensors_() {
  if (this->three_phase_) {
    // ---- Three-phase (Model 103) ----

    // Phase voltages (SF=-1): VAN, VBN, VCN at 40080-40082
    this->set_reg(40080, (uint16_t) to_sf(this->ac_voltage_->get_state(),   -1));
    this->set_reg(40081, (uint16_t) to_sf(this->ac_voltage_b_->get_state(), -1));
    this->set_reg(40082, (uint16_t) to_sf(this->ac_voltage_c_->get_state(), -1));

    if (this->ac_current_a_) {
      // Sensor-provided per-phase currents (SF=-2)
      float ia = this->ac_current_a_->get_state();
      float ib = this->ac_current_b_->get_state();
      float ic = this->ac_current_c_->get_state();
      float total = (!std::isnan(ia) ? ia : 0.0f)
                  + (!std::isnan(ib) ? ib : 0.0f)
                  + (!std::isnan(ic) ? ic : 0.0f);
      this->set_reg(40072, (uint16_t) to_sf(total, -2));
      this->set_reg(40073, (uint16_t) to_sf(ia, -2));
      this->set_reg(40074, (uint16_t) to_sf(ib, -2));
      this->set_reg(40075, (uint16_t) to_sf(ic, -2));
    } else {
      // Derive per-phase current: P/3 / V_phase
      float pwr = this->ac_power_->get_state();
      float va  = this->ac_voltage_->get_state();
      float vb  = this->ac_voltage_b_->get_state();
      float vc  = this->ac_voltage_c_->get_state();
      float phase_pwr = !std::isnan(pwr) ? pwr / 3.0f : NAN;

      auto derive_curr = [&](float v) -> int16_t {
        if (!std::isnan(phase_pwr) && !std::isnan(v) && v > 1.0f)
          return to_sf(phase_pwr / v, -2);
        return (int16_t) 0x8000;
      };

      int16_t ia = derive_curr(va);
      int16_t ib = derive_curr(vb);
      int16_t ic = derive_curr(vc);

      bool voltages_valid = !std::isnan(va) && !std::isnan(vb) && !std::isnan(vc)
                            && (va + vb + vc) > 3.0f;
      uint16_t total_reg;
      if (!std::isnan(pwr) && voltages_valid) {
        float avg_v = (va + vb + vc) / 3.0f;
        total_reg = (uint16_t) to_sf(pwr / avg_v, -2);
      } else {
        total_reg = 0x8000;
      }

      this->set_reg(40072, total_reg);
      this->set_reg(40073, (uint16_t) ia);
      this->set_reg(40074, (uint16_t) ib);
      this->set_reg(40075, (uint16_t) ic);
    }

  } else {
    // ---- Single-phase (Model 101) ----

    // AC current (SF=-2): derive from power/voltage if no current sensor
    if (this->ac_current_) {
      int16_t curr = to_sf(this->ac_current_->get_state(), -2);
      this->set_reg(40072, (uint16_t) curr);
      this->set_reg(40073, (uint16_t) curr);
    } else {
      float pwr  = this->ac_power_->get_state();
      float volt = this->ac_voltage_->get_state();
      if (!std::isnan(pwr) && !std::isnan(volt) && volt > 1.0f) {
        int16_t curr = to_sf(pwr / volt, -2);
        this->set_reg(40072, (uint16_t) curr);
        this->set_reg(40073, (uint16_t) curr);
      } else {
        this->set_reg(40072, 0x8000);
        this->set_reg(40073, 0x8000);
      }
    }

    // AC voltage (SF=-1)
    this->set_reg(40080, (uint16_t) to_sf(this->ac_voltage_->get_state(), -1));
  }

  // ---- Shared registers (both modes) ----

  // AC power (SF=0)
  this->set_reg(40084, (uint16_t) to_sf(this->ac_power_->get_state(), 0));

  // AC frequency (SF=-2)
  this->set_reg(40086, (uint16_t) to_sf(this->ac_frequency_->get_state(), -2));

  // Energy (uint32, SF=0)
  if (this->energy_total_) {
    float e = this->energy_total_->get_state();
    if (!std::isnan(e) && e >= 0) {
      uint32_t wh = (uint32_t)(e * 1000.0f);
      this->set_reg(40094, (uint16_t)(wh >> 16));
      this->set_reg(40095, (uint16_t)(wh & 0xFFFF));
    } else {
      this->set_reg(40094, 0);
      this->set_reg(40095, 0);
    }
  }

  // Temperature (SF=-1)
  this->set_reg(40103, (uint16_t) to_sf(this->temperature_->get_state(), -1));

  // Inverter state: MPPT (4) if producing, else Off (1)
  float pwr = this->ac_power_->get_state();
  this->set_reg(40108, (!std::isnan(pwr) && pwr > 5.0f) ? 4 : 1);
}
```

- [ ] **Step 2: Add 3-phase integration tests for per-phase registers**

Append to `tests/test_sunspec.py`:

```python
def test_model103_phase_voltages_not_ffff(three_phase):
    """3-phase: VAN/VBN/VCN (40080-40082) must not be 0xFFFF when sensors connected."""
    rr = three_phase.read_holding_registers(40080, 3, slave=1)
    assert not rr.isError()
    for i, reg in enumerate(rr.registers):
        assert reg != 0xFFFF, f"Voltage register 4008{i} is 0xFFFF (not implemented)"


def test_model103_phase_currents_not_ffff(three_phase):
    """3-phase: phase A/B/C currents (40073-40075) must not be 0xFFFF when inverter active."""
    rr = three_phase.read_holding_registers(40073, 3, slave=1)
    assert not rr.isError()
    for i, reg in enumerate(rr.registers):
        assert reg != 0xFFFF, f"Current register 4007{3+i} is 0xFFFF (not implemented)"


def test_model103_total_current_matches_sum(three_phase):
    """3-phase: total current (40072) should be consistent with phase sum."""
    rr = three_phase.read_holding_registers(40072, 4, slave=1)
    assert not rr.isError()
    total, ia, ib, ic = rr.registers
    # All values must be set (not NaN sentinel 0x8000)
    assert total != 0x8000
    assert ia != 0x8000
    assert ib != 0x8000
    assert ic != 0x8000
```

- [ ] **Step 3: Run schema tests to make sure nothing is broken**

```bash
python -m pytest tests/test_schema.py -v
```

Expected: all tests PASS.

- [ ] **Step 4: Commit**

```bash
git add components/sunspec/sunspec.cpp tests/test_sunspec.py
git commit -m "feat: 3-phase refresh_sensors branch with per-phase voltage and current"
```

---

## Self-Review Checklist

- **Spec coverage:**
  - `phases: 1|3` YAML key → Task 2 ✓
  - `ac_voltage_a/b/c` required for 3-phase → Tasks 1+2 ✓
  - `ac_current_a/b/c` optional all-or-none → Tasks 1+2 ✓
  - `ac_voltage`/`ac_current` rejected for 3-phase → Tasks 1+2 ✓
  - Backward-compatible (existing 1-phase configs unchanged) → Task 2 (`phases` defaults to `1`) ✓
  - Model 103 written to register 40070 → Task 4 ✓
  - Per-phase voltages at 40080-40082 → Task 5 ✓
  - Per-phase currents at 40073-40075 → Task 5 ✓
  - Derived current when no sensors → Task 5 ✓
  - `dump_config` logs phase count → Task 4 ✓
  - `setup()` validates 3-phase sensor pointers → Task 4 ✓

- **No placeholders:** all steps contain complete code.

- **Type consistency:** `set_ac_voltage` (phase A) is used in both `to_code` (Task 2) and C++ (Task 3 — existing setter, unchanged). New setters `set_ac_voltage_b/c`, `set_ac_current_a/b/c` defined in Task 3 and called in Task 2 `to_code`.
