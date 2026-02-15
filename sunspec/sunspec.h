#pragma once

#include "esphome/core/component.h"
#include "esphome/core/controller.h"
#include "esphome/core/helpers.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/modbus/modbus.h"
#include "esphome/components/modbus_tcp/modbus_tcp.h"

#include <string>
#include <vector>

namespace esphome {
namespace sunspec {

// SunSpec sentinel values for "not implemented"
static constexpr int16_t SUNSPEC_INT16_NI = static_cast<int16_t>(0x8000);    // INT16 not implemented
static constexpr uint16_t SUNSPEC_UINT16_NI = 0xFFFF;                        // UINT16 not implemented
static constexpr int32_t SUNSPEC_INT32_NI = static_cast<int32_t>(0x80000000);  // INT32 not implemented
static constexpr uint32_t SUNSPEC_UINT32_NI = 0xFFFFFFFF;                    // UINT32 not implemented

/// @brief SunSpec server implementation for PV inverter emulation
/// Implements SunSpec Model 1 (Common) and Model 101 (Single Phase Inverter)
class SunSpecServer : public Component, public Controller, public modbus::ModbusDevice {
 public:
  SunSpecServer();
  ~SunSpecServer() = default;

  void setup() override;
  void loop() override {}
  void dump_config() override;
  float get_setup_priority() const override;

  // Configuration setters (called from Python code generation)
  void set_modbus_tcp(modbus_tcp::ModbusTCP *tcp) { this->tcp_ = tcp; }
  void set_owns_modbus_tcp(bool owns);
  modbus_tcp::ModbusTCP *get_internal_modbus_tcp();
  void set_base_address(uint16_t addr) { this->base_address_ = addr; }
  void set_manufacturer(const std::string &manufacturer) { this->manufacturer_ = manufacturer; }
  void set_model(const std::string &model) { this->model_ = model; }
  void set_serial_number(const std::string &serial) { this->serial_number_ = serial; }
  void set_version(const std::string &version) { this->version_ = version; }

  // Model 101 - Inverter sensor setters
  void set_ac_power_sensor(sensor::Sensor *sensor) { this->ac_power_sensor_ = sensor; }
  void set_ac_voltage_sensor(sensor::Sensor *sensor) { this->ac_voltage_sensor_ = sensor; }
  void set_ac_current_sensor(sensor::Sensor *sensor) { this->ac_current_sensor_ = sensor; }
  void set_ac_frequency_sensor(sensor::Sensor *sensor) { this->ac_frequency_sensor_ = sensor; }
  void set_dc_power_sensor(sensor::Sensor *sensor) { this->dc_power_sensor_ = sensor; }
  void set_dc_voltage_sensor(sensor::Sensor *sensor) { this->dc_voltage_sensor_ = sensor; }
  void set_dc_current_sensor(sensor::Sensor *sensor) { this->dc_current_sensor_ = sensor; }
  void set_temperature_sensor(sensor::Sensor *sensor) { this->temperature_sensor_ = sensor; }

  // Controller interface - receive sensor updates
  void on_sensor_update(sensor::Sensor *obj) override;

  // ModbusDevice interface - handle Modbus requests
  void on_modbus_data(const std::vector<uint8_t> &data) override {}
  void on_modbus_read_registers(uint8_t function_code, uint16_t start_address,
                                 uint16_t number_of_registers) override;

 protected:
  modbus_tcp::ModbusTCP *tcp_{nullptr};
  modbus_tcp::ModbusTCP internal_tcp_;  // Internal ModbusTCP instance (only used if owns_tcp_ is true)
  bool owns_tcp_{false};
  uint16_t base_address_{40000};
  uint8_t unit_address_{1};

  // Model 1 - Common data
  std::string manufacturer_;
  std::string model_;
  std::string serial_number_;
  std::string version_;

  // Model 101 - Sensor references
  sensor::Sensor *ac_power_sensor_{nullptr};
  sensor::Sensor *ac_voltage_sensor_{nullptr};
  sensor::Sensor *ac_current_sensor_{nullptr};
  sensor::Sensor *ac_frequency_sensor_{nullptr};
  sensor::Sensor *dc_power_sensor_{nullptr};
  sensor::Sensor *dc_voltage_sensor_{nullptr};
  sensor::Sensor *dc_current_sensor_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};

  // Cached scaled values (updated by on_sensor_update)
  int16_t ac_power_scaled_{SUNSPEC_INT16_NI};
  uint16_t ac_voltage_scaled_{SUNSPEC_UINT16_NI};
  uint16_t ac_current_scaled_{SUNSPEC_UINT16_NI};
  uint16_t ac_frequency_scaled_{SUNSPEC_UINT16_NI};
  int16_t dc_power_scaled_{SUNSPEC_INT16_NI};
  uint16_t dc_voltage_scaled_{SUNSPEC_UINT16_NI};
  uint16_t dc_current_scaled_{SUNSPEC_UINT16_NI};
  int16_t temperature_scaled_{SUNSPEC_INT16_NI};

  // Helper methods
  void build_register_map_();
  uint16_t read_register_(uint16_t address);
  void send_read_response_(uint8_t function_code, uint16_t start_address, uint16_t count);

  // Scaling helpers
  int16_t scale_int16_(float value, int16_t scale_factor);
  uint16_t scale_uint16_(float value, int16_t scale_factor);

  // String to register conversion
  void string_to_registers_(const std::string &str, uint16_t *regs, size_t max_regs);

  // Register layout offsets (relative to base_address)
  static constexpr uint16_t SUNSPEC_ID_OFFSET = 0;          // "SunS" identifier
  static constexpr uint16_t MODEL_1_OFFSET = 2;             // Model 1 header starts at offset 2
  static constexpr uint16_t MODEL_1_DATA_OFFSET = 4;        // Model 1 data starts at offset 4
  static constexpr uint16_t MODEL_1_LENGTH = 66;            // Model 1 is 66 registers long
  static constexpr uint16_t MODEL_101_OFFSET = 70;          // Model 101 header (after Model 1)
  static constexpr uint16_t MODEL_101_DATA_OFFSET = 72;     // Model 101 data
  static constexpr uint16_t MODEL_101_LENGTH = 50;          // Model 101 is 50 registers long
  static constexpr uint16_t TERMINATOR_OFFSET = 122;        // End marker
};

}  // namespace sunspec
}  // namespace esphome
