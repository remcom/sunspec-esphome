#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/controller.h"
#include "esphome/core/helpers.h"
#include "esphome/components/sensor/sensor.h"
#include "modbus.h"
#include "modbus_tcp.h"

#include <array>
#include <string>
#include <vector>

namespace esphome {
namespace sunspec {

// SunSpec sentinel values for "not implemented"
static constexpr int16_t SUNSPEC_INT16_NI = static_cast<int16_t>(0x8000);  // int16 / sunssf not implemented
static constexpr uint16_t SUNSPEC_UINT16_NI = 0xFFFF;                      // uint16 / enum16 not implemented

// SunSpec inverter operating states (St)
static constexpr uint16_t SUNSPEC_ST_OFF = 1;
static constexpr uint16_t SUNSPEC_ST_SLEEPING = 2;
static constexpr uint16_t SUNSPEC_ST_MPPT = 4;

enum class PointType : uint8_t { TYPE_UINT16, TYPE_INT16, TYPE_ACC32 };

/// A live measurement point: maps a sensor to its register slot(s)
struct Point {
  sensor::Sensor *sensor;
  uint16_t reg;         // absolute register image index
  uint16_t mirror_reg;  // second register kept equal to reg (0 = none)
  PointType type;
  int16_t scale_factor;
  uint32_t last_update{0};
  bool has_value{false};
};

/// @brief SunSpec server implementation for PV inverter emulation
/// Implements SunSpec Model 1 (Common), Model 101/103 (Single/Three Phase
/// Inverter) and optionally Model 123 (Immediate Controls)
class SunSpecServer : public Component, public Controller, public modbus::ModbusDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // Configuration setters (called from Python code generation)
  void set_port(uint16_t port) { this->tcp_.set_port(port); }
  void set_max_connections(uint8_t max_connections) { this->tcp_.set_max_connections(max_connections); }
  void set_base_address(uint16_t addr) { this->base_address_ = addr; }
  void set_unit_address(uint8_t addr) { this->address_ = addr; }
  void set_stale_timeout(uint32_t timeout_ms) { this->stale_timeout_ms_ = timeout_ms; }
  void set_model_id(uint16_t model_id) { this->model_id_ = model_id; }
  void set_controls_enabled(bool enabled) { this->controls_enabled_ = enabled; }
  void set_manufacturer(const std::string &manufacturer) { this->manufacturer_ = manufacturer; }
  void set_model(const std::string &model) { this->model_ = model; }
  void set_serial_number(const std::string &serial) { this->serial_number_ = serial; }
  void set_version(const std::string &version) { this->version_ = version; }

  // Model 101/103 - Inverter sensor setters
  void set_ac_power_sensor(sensor::Sensor *sensor) { this->ac_power_sensor_ = sensor; }
  void set_ac_voltage_sensor(sensor::Sensor *sensor) { this->ac_voltage_sensor_ = sensor; }
  void set_ac_current_sensor(sensor::Sensor *sensor) { this->ac_current_sensor_ = sensor; }
  void set_ac_frequency_sensor(sensor::Sensor *sensor) { this->ac_frequency_sensor_ = sensor; }
  void set_dc_power_sensor(sensor::Sensor *sensor) { this->dc_power_sensor_ = sensor; }
  void set_dc_voltage_sensor(sensor::Sensor *sensor) { this->dc_voltage_sensor_ = sensor; }
  void set_dc_current_sensor(sensor::Sensor *sensor) { this->dc_current_sensor_ = sensor; }
  void set_temperature_sensor(sensor::Sensor *sensor) { this->temperature_sensor_ = sensor; }
  void set_energy_sensor(sensor::Sensor *sensor) { this->energy_sensor_ = sensor; }

  // Model 103 - per-phase sensor setters
  void set_ac_current_phase_a_sensor(sensor::Sensor *sensor) { this->ac_current_phase_[0] = sensor; }
  void set_ac_current_phase_b_sensor(sensor::Sensor *sensor) { this->ac_current_phase_[1] = sensor; }
  void set_ac_current_phase_c_sensor(sensor::Sensor *sensor) { this->ac_current_phase_[2] = sensor; }
  void set_ac_voltage_phase_a_sensor(sensor::Sensor *sensor) { this->ac_voltage_phase_[0] = sensor; }
  void set_ac_voltage_phase_b_sensor(sensor::Sensor *sensor) { this->ac_voltage_phase_[1] = sensor; }
  void set_ac_voltage_phase_c_sensor(sensor::Sensor *sensor) { this->ac_voltage_phase_[2] = sensor; }

  // Model 123 - Immediate Controls
  void add_on_power_limit_callback(std::function<void(float, bool)> &&callback) {
    this->power_limit_callback_.add(std::move(callback));
  }
  /// Last commanded power limit in percent of nameplate power (WMaxLimPct)
  float get_power_limit_pct() const { return this->registers_[REG_WMAXLIMPCT] * 0.01f; }
  /// Whether the commanded power limit is enabled (WMaxLim_Ena)
  bool get_power_limit_enabled() const { return this->registers_[REG_WMAXLIM_ENA] == 1; }

  /// Number of connected Modbus clients (for diagnostics)
  uint8_t get_client_count() const { return this->tcp_.get_client_count(); }

  // Controller interface - receive sensor updates
  void on_sensor_update(sensor::Sensor *obj) override;

  // ModbusDevice interface - handle Modbus requests
  void on_modbus_read_registers(uint8_t function_code, uint16_t start_address,
                                uint16_t number_of_registers) override;
  void on_modbus_write_registers(uint8_t function_code, uint16_t start_address,
                                 const std::vector<uint16_t> &values) override;

 protected:
  // Register layout offsets (relative to base_address)
  static constexpr uint16_t SUNSPEC_ID_OFFSET = 0;       // "SunS" identifier
  static constexpr uint16_t MODEL_1_OFFSET = 2;          // Model 1 header
  static constexpr uint16_t MODEL_1_DATA_OFFSET = 4;     // Model 1 data
  static constexpr uint16_t MODEL_1_LENGTH = 66;         // Model 1 data length
  static constexpr uint16_t MODEL_10X_OFFSET = 70;       // Model 101/103 header (after Model 1)
  static constexpr uint16_t MODEL_10X_DATA_OFFSET = 72;  // Model 101/103 data
  static constexpr uint16_t MODEL_10X_LENGTH = 50;       // Model 101/103 data length
  static constexpr uint16_t MODEL_123_OFFSET = 122;      // Model 123 header (only if controls enabled)
  static constexpr uint16_t MODEL_123_DATA_OFFSET = 124;
  static constexpr uint16_t MODEL_123_LENGTH = 24;
  static constexpr uint16_t MAX_REGISTERS = 150;  // largest possible image incl. terminator

  // Live measurement slots in the Model 101/103 data block (SunSpec point offsets)
  static constexpr uint16_t REG_AC_CURRENT = MODEL_10X_DATA_OFFSET + 0;        // A
  static constexpr uint16_t REG_AC_CURRENT_PHA = MODEL_10X_DATA_OFFSET + 1;    // AphA
  static constexpr uint16_t REG_AC_CURRENT_PHB = MODEL_10X_DATA_OFFSET + 2;    // AphB
  static constexpr uint16_t REG_AC_CURRENT_PHC = MODEL_10X_DATA_OFFSET + 3;    // AphC
  static constexpr uint16_t REG_AC_CURRENT_SF = MODEL_10X_DATA_OFFSET + 4;     // A_SF
  static constexpr uint16_t REG_AC_VOLTAGE_PHA = MODEL_10X_DATA_OFFSET + 8;    // PhVphA
  static constexpr uint16_t REG_AC_VOLTAGE_PHB = MODEL_10X_DATA_OFFSET + 9;    // PhVphB
  static constexpr uint16_t REG_AC_VOLTAGE_PHC = MODEL_10X_DATA_OFFSET + 10;   // PhVphC
  static constexpr uint16_t REG_AC_VOLTAGE_SF = MODEL_10X_DATA_OFFSET + 11;    // V_SF
  static constexpr uint16_t REG_AC_POWER = MODEL_10X_DATA_OFFSET + 12;         // W
  static constexpr uint16_t REG_AC_POWER_SF = MODEL_10X_DATA_OFFSET + 13;      // W_SF
  static constexpr uint16_t REG_AC_FREQUENCY = MODEL_10X_DATA_OFFSET + 14;     // Hz
  static constexpr uint16_t REG_AC_FREQUENCY_SF = MODEL_10X_DATA_OFFSET + 15;  // Hz_SF
  static constexpr uint16_t REG_ENERGY = MODEL_10X_DATA_OFFSET + 22;           // WH (acc32, 2 regs)
  static constexpr uint16_t REG_ENERGY_SF = MODEL_10X_DATA_OFFSET + 24;        // WH_SF
  static constexpr uint16_t REG_DC_CURRENT = MODEL_10X_DATA_OFFSET + 25;       // DCA
  static constexpr uint16_t REG_DC_CURRENT_SF = MODEL_10X_DATA_OFFSET + 26;    // DCA_SF
  static constexpr uint16_t REG_DC_VOLTAGE = MODEL_10X_DATA_OFFSET + 27;       // DCV
  static constexpr uint16_t REG_DC_VOLTAGE_SF = MODEL_10X_DATA_OFFSET + 28;    // DCV_SF
  static constexpr uint16_t REG_DC_POWER = MODEL_10X_DATA_OFFSET + 29;         // DCW
  static constexpr uint16_t REG_DC_POWER_SF = MODEL_10X_DATA_OFFSET + 30;      // DCW_SF
  static constexpr uint16_t REG_TEMPERATURE = MODEL_10X_DATA_OFFSET + 31;      // TmpCab
  static constexpr uint16_t REG_TEMPERATURE_SF = MODEL_10X_DATA_OFFSET + 35;   // Tmp_SF
  static constexpr uint16_t REG_STATE = MODEL_10X_DATA_OFFSET + 36;            // St
  static constexpr uint16_t REG_STATE_VENDOR = MODEL_10X_DATA_OFFSET + 37;     // StVnd

  // Model 123 slots
  static constexpr uint16_t REG_CONN = MODEL_123_DATA_OFFSET + 2;         // Conn
  static constexpr uint16_t REG_WMAXLIMPCT = MODEL_123_DATA_OFFSET + 3;   // WMaxLimPct
  static constexpr uint16_t REG_WMAXLIM_ENA = MODEL_123_DATA_OFFSET + 7;  // WMaxLim_Ena
  static constexpr uint16_t MODEL_123_WRITABLE = 21;  // data offsets 0-20 writable; SFs (21-23) read-only

  // Scale factors (value = register * 10^SF)
  static constexpr int16_t SF_CURRENT = -2;     // 0.01 A
  static constexpr int16_t SF_VOLTAGE = -1;     // 0.1 V
  static constexpr int16_t SF_POWER = 0;        // 1 W
  static constexpr int16_t SF_FREQUENCY = -2;   // 0.01 Hz
  static constexpr int16_t SF_TEMPERATURE = 0;  // 1 degC

  // Helper methods
  void build_points_();
  void build_register_map_();
  void write_string_(uint16_t offset, const std::string &str, size_t max_regs);
  void send_read_response_(uint8_t function_code, uint16_t start_address, uint16_t count);
  void check_stale_points_();

  // Scaling helpers - return the raw register encoding
  static uint16_t scale_int16_(float value, int16_t scale_factor);
  static uint16_t scale_uint16_(float value, int16_t scale_factor);

  modbus_tcp::ModbusTCP tcp_;  // Embedded ModbusTCP server
  uint16_t base_address_{40000};
  uint16_t model_id_{101};
  bool controls_enabled_{false};
  uint32_t stale_timeout_ms_{300000};
  uint16_t terminator_offset_{122};
  uint16_t total_registers_{124};

  // Model 1 - Common data
  std::string manufacturer_;
  std::string model_;
  std::string serial_number_;
  std::string version_;

  // Model 101/103 - Sensor references
  sensor::Sensor *ac_power_sensor_{nullptr};
  sensor::Sensor *ac_voltage_sensor_{nullptr};
  sensor::Sensor *ac_current_sensor_{nullptr};
  sensor::Sensor *ac_frequency_sensor_{nullptr};
  sensor::Sensor *dc_power_sensor_{nullptr};
  sensor::Sensor *dc_voltage_sensor_{nullptr};
  sensor::Sensor *dc_current_sensor_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *energy_sensor_{nullptr};
  sensor::Sensor *ac_current_phase_[3]{nullptr, nullptr, nullptr};
  sensor::Sensor *ac_voltage_phase_[3]{nullptr, nullptr, nullptr};

  // Precomputed register image; static content built once in setup(),
  // live slots patched by on_sensor_update()
  std::array<uint16_t, MAX_REGISTERS> registers_{};
  std::vector<Point> points_;
  uint32_t last_stale_check_{0};

  CallbackManager<void(float, bool)> power_limit_callback_;
};

class PowerLimitTrigger : public Trigger<float, bool> {
 public:
  explicit PowerLimitTrigger(SunSpecServer *parent) {
    parent->add_on_power_limit_callback(
        [this](float level, bool enabled) { this->trigger(level, enabled); });
  }
};

}  // namespace sunspec
}  // namespace esphome
