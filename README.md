# sunspec-esphome

Custom ESPHome external component that runs a **SunSpec Modbus TCP server** (port 502) directly on an ESP32. It reads live inverter data from existing ESPHome sensors and serves it to any SunSpec-compatible energy manager (Victron, SolarEdge, Fronius, etc.). It also accepts power limit commands from SunSpec clients and relays them back to the inverter over RS485 Modbus.

Built for a **Solis inverter** bridged via an ESP32 (m5stack-atom, ESP-IDF), but the sensor IDs and power limit register are configurable via YAML. Supports both single-phase and three-phase inverters.

## Features

- SunSpec Models 1 (Common), 101/103 (Single/three-phase inverter), 120 (Nameplate), 123 (Controls)
- Single-phase (Model 101) and three-phase (Model 103) support via `phases: 1|3`
- FC03 read holding registers
- FC06 / FC16 write — power limit (WMaxLimPct) and enable (WMaxLim_Ena)
- Power limit write-back to inverter over RS485 via `modbus_controller`
- Up to 2 simultaneous Modbus TCP clients
- ESP-IDF and Arduino frameworks on ESP32

## Installation

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/remcom/sunspec-esphome
      ref: master
    refresh: 0d
    components:
      - sunspec
```

## Configuration

**Single-phase (default):**

```yaml
sunspec:
  # Device identification (shown to SunSpec clients)
  manufacturer: "Solis"
  model: "S6-GR1P3K-M"
  serial_number: "12345678"
  version: "1.0.0"
  rated_power: 3000        # watts, max 32767

  # phases defaults to 1 — serves SunSpec Model 101
  ac_power: ac_power                 # required
  ac_voltage: ac_voltage             # required
  ac_frequency: ac_frequency         # required
  temperature: inverter_temp         # required
  ac_current: ac_current             # optional
  energy_total: energy_total_wh      # optional

  # Power limit write-back — option A: via a number entity (recommended)
  power_limit_number_id: power_limit # ESPHome number entity ID (0–100 = limit %, 110 = unlimited)

  # Power limit write-back — option B: direct Modbus register write (legacy)
  # modbus_controller_id: modbus_master
  # power_limit_register: 3051       # Solis RS485 register for power limit
```

**Three-phase (Model 103) — experimental, not hardware-tested:**

```yaml
sunspec:
  manufacturer: "Solis"
  model: "S6-GR3P8K-M"
  serial_number: "12345678"
  version: "1.0.0"
  rated_power: 8000

  phases: 3                          # serves SunSpec Model 103
  ac_voltage_a: ac_voltage_a         # required — phase A voltage (V)
  ac_voltage_b: ac_voltage_b         # required — phase B voltage (V)
  ac_voltage_c: ac_voltage_c         # required — phase C voltage (V)
  ac_current_a: ac_current_a         # optional — if omitted, current is derived from power/voltage
  ac_current_b: ac_current_b         # optional — must configure all three or none
  ac_current_c: ac_current_c         # optional
  ac_power: ac_power                 # required
  ac_frequency: ac_frequency         # required
  temperature: inverter_temp         # required
  energy_total: energy_total_wh      # optional
```

The `power_limit_number_id` approach routes the limit through an existing `modbus_controller` number entity. Define it alongside your other number entities:

```yaml
number:
  - platform: modbus_controller
    modbus_controller_id: modbus_master
    id: power_limit
    name: Limit output power
    register_type: holding
    address: 3051           # = 3052 - 1 (Solis)
    value_type: S_WORD
    unit_of_measurement: '%'
    entity_category: config
    icon: mdi:percent
    skip_updates: 10
    mode: box
    min_value: 0
    max_value: 110
    multiply: 100
```

### Options

| Key | Required | Description |
|-----|----------|-------------|
| `manufacturer` | yes | Manufacturer string (max 32 chars) |
| `model` | yes | Model string (max 32 chars) |
| `serial_number` | yes | Serial number string (max 32 chars) |
| `version` | yes | Firmware version string (max 16 chars) |
| `rated_power` | yes | Inverter rated power in watts (int, max 32767) |
| `phases` | no | `1` (default) or `3` — selects Model 101 or 103 |
| `ac_power` | yes | ESPHome sensor ID for AC power (W) |
| `ac_frequency` | yes | ESPHome sensor ID for AC frequency (Hz) |
| `temperature` | yes | ESPHome sensor ID for inverter temperature (°C) |
| `ac_voltage` | phases=1 | ESPHome sensor ID for AC voltage (V) |
| `ac_current` | no | ESPHome sensor ID for AC current (A) — single-phase only |
| `ac_voltage_a` | phases=3 | Phase A voltage sensor (V) |
| `ac_voltage_b` | phases=3 | Phase B voltage sensor (V) |
| `ac_voltage_c` | phases=3 | Phase C voltage sensor (V) |
| `ac_current_a` | no | Phase A current sensor (A) — all three or none |
| `ac_current_b` | no | Phase B current sensor (A) — all three or none |
| `ac_current_c` | no | Phase C current sensor (A) — all three or none |
| `energy_total` | no | ESPHome sensor ID for lifetime energy (Wh) |
| `power_limit_number_id` | no | ESPHome `number` entity ID to route power limit through (recommended) |
| `modbus_controller_id` | no | ID of your `modbus_controller` component (required when using `power_limit_register`) |
| `power_limit_register` | no | RS485 register address for direct power limit write (legacy) |

## SunSpec Register Map

| Range | Model | Content |
|-------|-------|---------|
| 40000–40069 | 1 | Common block (manufacturer, model, serial, version) |
| 40070–40121 | 101 or 103 | Inverter block — Model 101 (single-phase) or 103 (three-phase) |
| 40122–40149 | 120 | Nameplate (DER type, rated power) |
| 40150–40175 | 123 | Controls (WMaxLimPct @ 40155, WMaxLim_Ena @ 40159) |
| 40176–40179 | — | End marker |

**Three-phase register assignments (Model 103):**

| Register | Content |
|----------|---------|
| 40072 | Total AC current (SF=-2) |
| 40073–40075 | Phase A/B/C current (SF=-2) |
| 40080–40082 | Phase A/B/C voltage VAN/VBN/VCN (SF=-1) |

If `ac_current_a/b/c` sensors are not configured, per-phase currents are derived from `ac_power / 3 / V_phase`.

## Power Limiting

Write `WMaxLimPct` (register 40155, range 0–100) and `WMaxLim_Ena` (register 40159, `1` = enabled):

**Via number entity (`power_limit_number_id`, recommended):**
- When `WMaxLim_Ena = 1`: sets the number entity to `WMaxLimPct` (0–100)
- When `WMaxLim_Ena = 0`: sets the number entity to `110` (maps to "unlimited" / full power)
- The number entity's own `multiply` and write logic handles the actual Modbus write

**Via direct register write (`power_limit_register`, legacy):**
- When `WMaxLim_Ena = 1`: queues a write of `WMaxLimPct` to `power_limit_register` on the inverter
- When `WMaxLim_Ena = 0`: queues a write of `100` (full power restore)
- Writes are asynchronous — executed on the Modbus controller's next poll cycle

On ESP32 reboot, `WMaxLim_Ena` defaults to `0` and `WMaxLimPct` defaults to `100`. The inverter's own power limit state is not read back on startup — re-apply any desired limit after reboot.

## Scope / Limitations

- One inverter instance per ESP32
- No persistent energy counter across reboots
- No WMaxLimPct auto-revert timer (field present but always 0)
- Three-phase support (Model 103) is **not hardware-tested** — use with caution and report issues
- Three-phase derived currents assume unity power factor per phase
