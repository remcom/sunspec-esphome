# Three-Phase Inverter Support Design

**Date:** 2026-04-06  
**Branch:** extend-functions  
**Status:** Approved

## Overview

Extend the `sunspec` ESPHome component to support 3-phase inverters alongside the existing single-phase support. The phase count is selected at YAML configuration time via a `phases` key. Existing single-phase configs remain unchanged.

## YAML Schema

A new optional `phases` key (default `1`, valid values `1` or `3`) selects the inverter model.

**Single-phase (existing, unchanged):**
```yaml
sunspec:
  phases: 1          # or omit — default
  ac_voltage: id(sensor_v)
  ac_current: id(sensor_i)   # optional
  ac_power: id(sensor_p)
  ac_frequency: id(sensor_f)
  temperature: id(sensor_t)
```

**Three-phase:**
```yaml
sunspec:
  phases: 3
  ac_voltage_a: id(sensor_va)
  ac_voltage_b: id(sensor_vb)
  ac_voltage_c: id(sensor_vc)
  ac_current_a: id(sensor_ia)   # optional
  ac_current_b: id(sensor_ib)   # optional
  ac_current_c: id(sensor_ic)   # optional
  ac_power: id(sensor_p)
  ac_frequency: id(sensor_f)
  temperature: id(sensor_t)
```

Shared keys (`ac_power`, `ac_frequency`, `temperature`, `energy_total`, all power limit options) work identically in both modes.

**Python validation rules:**
- `ac_voltage_a/b/c` are rejected when `phases: 1`
- `ac_voltage` is rejected when `phases: 3`
- All three `ac_voltage_a/b/c` must appear together when `phases: 3`
- `ac_current_a/b/c` must be all-or-none when `phases: 3`

## Register Layout

3-phase uses **SunSpec Model 103** instead of Model 101. The block starts at the same address (40070) with the same length (50 registers), so the subsequent Model 120, Model 123, and end marker blocks are unaffected.

| Address | Model 101 (1-phase)      | Model 103 (3-phase)      |
|---------|--------------------------|--------------------------|
| 40070   | Model ID = 101           | Model ID = 103           |
| 40071   | Length = 50              | Length = 50              |
| 40072   | Total current            | Total current            |
| 40073   | Phase A current          | Phase A current          |
| 40074   | 0xFFFF                   | Phase B current          |
| 40075   | 0xFFFF                   | Phase C current          |
| 40076   | Current SF = -2          | Current SF = -2          |
| 40077–79 | Line-to-line (0xFFFF)  | Line-to-line (0xFFFF)    |
| 40080   | Voltage AN               | Voltage AN (phase A)     |
| 40081   | 0xFFFF                   | Voltage BN (phase B)     |
| 40082   | 0xFFFF                   | Voltage CN (phase C)     |
| 40083–121 | Same as Model 101      | Same as Model 103        |

**Current derivation (when no current sensors configured):**
- Per-phase current = `ac_power / 3 / voltage_x` (per phase)
- Total current = sum of derived phase currents

## C++ Class Changes

### `sunspec.h` additions

New flag and per-phase sensor pointers:
```cpp
bool three_phase_{false};
sensor::Sensor *ac_voltage_b_{nullptr};
sensor::Sensor *ac_voltage_c_{nullptr};
sensor::Sensor *ac_current_a_{nullptr};
sensor::Sensor *ac_current_b_{nullptr};
sensor::Sensor *ac_current_c_{nullptr};
```

The existing `ac_voltage_` serves as phase A voltage in both modes. The existing `ac_current_` remains the single-phase current sensor.

New setters: `set_three_phase()`, `set_ac_voltage_b()`, `set_ac_voltage_c()`, `set_ac_current_a()`, `set_ac_current_b()`, `set_ac_current_c()`.

### `sunspec.cpp` changes

- **`init_static_registers_()`:** writes `101` or `103` at 40070 based on `three_phase_` flag.
- **`setup()` validation:** when `three_phase_`, asserts `ac_voltage_b_` and `ac_voltage_c_` are non-null.
- **`refresh_sensors_()`:** branches on `three_phase_`:
  - `false`: current behavior unchanged
  - `true`: fills 40073–40075 (per-phase currents) and 40080–40082 (per-phase voltages), derives missing values as above
- **`dump_config()`:** logs phase count.

No changes to TCP server, frame handling, or power limit logic.

## Out of Scope

- Split-phase (2-phase) support
- Per-phase energy metering
- Per-phase power reporting
