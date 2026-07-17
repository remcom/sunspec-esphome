# sunspec-esphome

An [ESPHome](https://esphome.io) external component that emulates a **SunSpec-compatible PV inverter** over Modbus TCP. Point any SunSpec client (Home Assistant's SunSpec integration, pysunspec2, energy managers, zero-export controllers, wallboxes) at your ESP32 and it will see a standards-compliant inverter fed by your own ESPHome sensors.

**ESP32 only** (uses lwip sockets). Requires ESPHome 2026.7 or newer.

## Features

- **Model 1 (Common)** — manufacturer, model, serial number, version
- **Model 101 (Single Phase Inverter)** or **Model 103 (Three Phase Inverter)** — AC/DC power, voltage, current, frequency, temperature, lifetime energy, all fed from ESPHome sensors
- **Model 123 (Immediate Controls)** — optional; accepts power-limit commands (FC 0x06 / 0x10 writes to `WMaxLimPct` / `WMaxLim_Ena`) and fires an ESPHome automation, so you can use the device as the receiving end of a zero-export / curtailment controller
- Operating state (`St`) derived from AC power: producing → `MPPT`, ~0 W → `SLEEPING`, sensor stale → `OFF`
- Stale-data protection: points revert to the SunSpec "not implemented" sentinel when their sensor stops updating
- Modbus TCP server with multiple client connections, idle-connection reaping, keepalive, and proper exception responses

## Installation

```yaml
external_components:
  - source: github://remcom/sunspec-esphome
    components: [sunspec]
```

## Example

```yaml
sunspec:
  - manufacturer: "ESPHome"
    model: "Garage PV"
    serial_number: "SN12345678"
    inverter_single_phase:
      ac_power: pv_ac_power        # W
      ac_voltage: pv_ac_voltage    # V
      ac_current: pv_ac_current    # A
      ac_frequency: pv_frequency   # Hz
      dc_power: pv_dc_power        # W
      temperature: pv_temperature  # °C
      energy: pv_energy            # Wh, lifetime total
    controls:
      on_power_limit:
        - logger.log:
            format: "Limit %.2f%% (enabled=%d)"
            args: ["level", "enabled"]
```

All sensors are optional and must publish **base units**: W, V, A, Hz, °C, Wh. Unconfigured points read as SunSpec "not implemented".

## Configuration reference

| Option | Default | Description |
| --- | --- | --- |
| `port` | `502` | TCP port to listen on (must be unique per server instance) |
| `max_connections` | `4` | Concurrent Modbus TCP clients (1–8) |
| `base_address` | `40000` | Modbus register base of the SunSpec map |
| `address` | `1` | Modbus unit ID (unit ID `255` is also always accepted) |
| `stale_timeout` | `5min` | Revert points to "not implemented" when their sensor hasn't updated for this long (`0s` disables) |
| `manufacturer` | required | Model 1 `Mn` (max 32 chars) |
| `model` | required | Model 1 `Md` (max 32 chars) |
| `serial_number` | — | Model 1 `SN` (max 32 chars) |
| `version` | `"1.0"` | Model 1 `Vr` (max 16 chars) |
| `inverter_single_phase` | — | Model 101 sensor block (see below) |
| `inverter_three_phase` | — | Model 103 sensor block (mutually exclusive with single phase) |
| `controls` | — | Enables Model 123; supports `on_power_limit` automations |

### Sensor blocks

`inverter_single_phase`: `ac_power`, `ac_voltage`, `ac_current`, `ac_frequency`, `dc_power`, `dc_voltage`, `dc_current`, `temperature`, `energy`

`inverter_three_phase`: same, minus `ac_voltage`, plus `ac_current_phase_a/b/c` and `ac_voltage_phase_a/b/c` (phase-to-neutral). `ac_current` is the total.

### `on_power_limit` trigger

Fires whenever a client writes `WMaxLimPct` or `WMaxLim_Ena`. Arguments:

- `level` (`float`) — commanded limit in percent of nameplate power (0.00–100.00)
- `enabled` (`bool`) — whether the limit is active

The latest values are also available in lambdas via `id(my_server).get_power_limit_pct()` and `id(my_server).get_power_limit_enabled()`.

### Diagnostics

`id(my_server).get_client_count()` returns the number of connected clients — handy in a template sensor:

```yaml
sensor:
  - platform: template
    name: "SunSpec Clients"
    lambda: "return id(my_server).get_client_count();"
    update_interval: 60s
```

## Register map

Offsets relative to `base_address` (default 40000):

| Offset | Content |
| --- | --- |
| 0–1 | `SunS` well-known identifier |
| 2–3 | Model 1 header (ID=1, L=66) |
| 4–69 | Model 1 data: Mn(16) Md(16) Opt(8) Vr(8) SN(16) DA(1) Pad(1) |
| 70–71 | Model 101/103 header (ID=101 or 103, L=50) |
| 72–121 | Inverter data (A, AphA–C, A_SF, PPV/PhV block, V_SF, W, W_SF, Hz, Hz_SF, VA, VAr, PF, WH, DC block, temperatures, St, StVnd, events) |
| 122–123 | Model 123 header (ID=123, L=24) — only with `controls:` |
| 124–147 | Model 123 data (Conn, WMaxLimPct, WMaxLim_Ena, …; scale factors read-only) |
| 122–123 or 148–149 | Terminator (0xFFFF, 0) |

Function codes 0x03/0x04 (read) are supported everywhere; 0x06/0x10 (write) only within Model 123 data offsets 0–20 when `controls:` is enabled. Everything else gets a proper Modbus exception.

## Scaling

| Point | Scale factor | Resolution |
| --- | --- | --- |
| Currents | −2 | 0.01 A |
| Voltages | −1 | 0.1 V |
| Power | 0 | 1 W (max ±32.767 kW; clamped with a warning) |
| Frequency | −2 | 0.01 Hz |
| Temperature | 0 | 1 °C |
| Energy | 0 | 1 Wh (acc32) |

## Testing

`tests/test-esp32.yaml` compiles both a single-phase server with controls and a three-phase server; CI validates and compiles it on every push. For an end-to-end check, point [pysunspec2](https://github.com/sunspec/pysunspec2) or Home Assistant's SunSpec integration at port 502.
