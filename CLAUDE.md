# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

## Project Overview

**sunspec-esphome** is an ESPHome external component that runs a SunSpec Modbus TCP server (port 502) on an ESP32. It reads live inverter data from existing ESPHome sensors and serves it to SunSpec-compatible energy managers (Victron, SolarEdge, Fronius, etc.). It also accepts power limit commands and relays them back to the inverter over RS485 Modbus.

Built for a Solis single-phase inverter bridged via an ESP32 running ESP-IDF.

## Repository Structure

```
components/sunspec/
├── __init__.py      # Config schema and code generation
├── sunspec.h        # C++ header
└── sunspec.cpp      # C++ implementation
tests/
├── __init__.py
├── test_schema.py
└── test_sunspec.py
```

## ESPHome Component Standards

This project follows the official ESPHome coding conventions. Key rules:

### Python (`__init__.py`)

- Import constants from `esphome.const` where they exist; define local `CONF_*` strings only when no matching constant exists.
- Declare `DEPENDENCIES = [...]` for always-required components. Optional components (used only when configured) must NOT be listed — that would force-load them unconditionally.
- Add `cv.only_with_esp_idf` to `CONFIG_SCHEMA` for any component using POSIX socket APIs — they are ESP-IDF only.
- Use `cv.All(schema, validator_fn)` for cross-field validation (e.g. requiring two options to appear together).
- Add `cv.Length(max=N)` validators for strings that map to fixed-size SunSpec registers.

### C++ conventions (from ESPHome instructions.md)

- **`this->` prefix**: All class member access must use `this->member_`. No bare `member_` in method bodies.
- **Namespace**: Use inline C++20 style — `namespace esphome::sunspec {` / `}  // namespace esphome::sunspec`.
- **`dump_config()`**: Every component must implement `void dump_config() override` to log its configuration at boot via `ESP_LOGCONFIG`.
- **`constexpr`**: Prefer `constexpr` over `static const` for compile-time constants.
- **No VLAs**: Variable-length arrays are not standard C++. Use fixed-size arrays sized to the known maximum (e.g. `uint8_t pdu[242]` for a 120-register FC03 response).
- **TAG in `.cpp`**: `static const char *const TAG` belongs in the `.cpp` file only, never the header.
- **POSIX headers in `.cpp`**: `sys/socket.h`, `netinet/in.h`, `arpa/inet.h` etc. belong in the `.cpp`, not the header.
- **Check syscall return values**: `fcntl()`, `send()`, and similar calls must have their return values checked and errors logged.
- **Blank line between function definitions**: Always separate function definitions with a blank line.

### SunSpec register layout

| Range      | Model | Content                                               |
|------------|-------|-------------------------------------------------------|
| 40000–40069 | 1    | Common block (manufacturer, model, serial, version)   |
| 40070–40121 | 101  | Single-phase inverter (power, voltage, current, freq, temp, energy, state) |
| 40122–40149 | 120  | Nameplate (DER type, rated power)                     |
| 40150–40175 | 123  | Controls (WMaxLimPct @ 40155, WMaxLim_Ena @ 40159)    |
| 40176–40179 | —    | End marker (0xFFFF)                                   |

String fields use big-endian uint16 register pairs (2 chars per register):
- Manufacturer, model, serial: 16 registers = 32 chars max
- Version, options: 8 registers = 16 chars max

## Running Tests

```bash
python -m pytest tests/
```

## Power Limit Write-back

Two modes, mutually exclusive (both or neither must be configured):

**Via number entity (recommended):**
- `WMaxLim_Ena = 1`: sets number entity to `WMaxLimPct` (0–100)
- `WMaxLim_Ena = 0`: sets number entity to `110` (maps to unlimited)

**Via direct Modbus register write (legacy):**
- `WMaxLim_Ena = 1`: writes `WMaxLimPct` to `power_limit_register`
- `WMaxLim_Ena = 0`: writes `100` to restore full power
