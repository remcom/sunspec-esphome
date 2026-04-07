#pragma once

#include <cmath>
#include <string>

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/number/number.h"

namespace esphome::sunspec {

static constexpr uint16_t BASE_ADDR         = 40000;
static constexpr uint16_t REG_COUNT         = 200;
static constexpr uint8_t  MAX_CLIENTS       = 2;
static constexpr uint16_t MAX_BUF           = 260;
static constexpr uint32_t CLIENT_TIMEOUT_MS = 10000;

// SunSpec register addresses
static constexpr uint16_t REG_WMAXLIMPCT  = 40155;
static constexpr uint16_t REG_WMAXLIM_ENA = 40159;

struct Client {
  int      fd{-1};
  uint8_t  buf[MAX_BUF];
  uint16_t buf_len{0};
  uint32_t last_recv_ms{0};
};

class SunspecComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // Setters called from __init__.py generated code
  void set_manufacturer(const std::string &s) { this->manufacturer_ = s; }
  void set_model(const std::string &s)        { this->model_ = s; }
  void set_serial_number(const std::string &s){ this->serial_number_ = s; }
  void set_version(const std::string &s)      { this->version_ = s; }
  void set_rated_power(uint16_t w)            { this->rated_power_ = w; }

  void set_ac_power(sensor::Sensor *s)     { this->ac_power_ = s; }
  void set_ac_voltage(sensor::Sensor *s)   { this->ac_voltage_ = s; }
  void set_ac_current(sensor::Sensor *s)   { this->ac_current_ = s; }
  void set_ac_frequency(sensor::Sensor *s) { this->ac_frequency_ = s; }
  void set_temperature(sensor::Sensor *s)  { this->temperature_ = s; }
  void set_energy_total(sensor::Sensor *s) { this->energy_total_ = s; }

  void set_three_phase(bool v)               { this->three_phase_ = v; }
  void set_ac_voltage_b(sensor::Sensor *s)   { this->ac_voltage_b_ = s; }
  void set_ac_voltage_c(sensor::Sensor *s)   { this->ac_voltage_c_ = s; }
  void set_ac_current_a(sensor::Sensor *s)   { this->ac_current_a_ = s; }
  void set_ac_current_b(sensor::Sensor *s)   { this->ac_current_b_ = s; }
  void set_ac_current_c(sensor::Sensor *s)   { this->ac_current_c_ = s; }

  void set_modbus_controller(modbus_controller::ModbusController *ctrl) { this->controller_ = ctrl; }
  void set_power_limit_register(uint16_t reg) { this->power_limit_register_ = reg; }
  void set_power_limit_number(number::Number *n) { this->power_limit_number_ = n; }

 protected:
  // Config
  std::string manufacturer_;
  std::string model_;
  std::string serial_number_;
  std::string version_;
  uint16_t    rated_power_{0};

  // Sensors (ac_power, ac_voltage, ac_frequency, temperature required; ac_current and energy_total optional)
  sensor::Sensor *ac_power_{nullptr};
  sensor::Sensor *ac_voltage_{nullptr};
  sensor::Sensor *ac_current_{nullptr};
  sensor::Sensor *ac_frequency_{nullptr};
  sensor::Sensor *temperature_{nullptr};
  sensor::Sensor *energy_total_{nullptr};

  bool            three_phase_{false};
  sensor::Sensor *ac_voltage_b_{nullptr};
  sensor::Sensor *ac_voltage_c_{nullptr};
  sensor::Sensor *ac_current_a_{nullptr};
  sensor::Sensor *ac_current_b_{nullptr};
  sensor::Sensor *ac_current_c_{nullptr};

  // Modbus write-back
  modbus_controller::ModbusController *controller_{nullptr};
  uint16_t power_limit_register_{0};
  number::Number *power_limit_number_{nullptr};

  // Register bank
  uint16_t registers_[REG_COUNT]{};

  // TCP server
  int    server_fd_{-1};
  Client clients_[MAX_CLIENTS];

  // Internal helpers
  void     init_static_registers_();
  void     encode_string_(uint16_t *dest, const std::string &s, uint8_t reg_count);
  void     refresh_sensors_();
  void     accept_clients_();
  void     process_client_(Client &c);
  void     handle_frame_(Client &c, uint16_t frame_len);
  void     send_response_(int fd, uint16_t txid, uint8_t uid, const uint8_t *pdu, uint8_t pdu_len);
  void     send_exception_(int fd, uint16_t txid, uint8_t uid, uint8_t fc, uint8_t code);
  void     apply_power_limit_();
  void     close_client_(Client &c);

  // Register helpers
  inline uint16_t get_reg(uint16_t addr)             { return this->registers_[addr - BASE_ADDR]; }
  inline void     set_reg(uint16_t addr, uint16_t v) { this->registers_[addr - BASE_ADDR] = v; }
  // Convert float to int16 with given scale factor (e.g. sf=-2 → multiply by 100)
  static int16_t to_sf(float val, int sf);
};

}  // namespace esphome::sunspec
