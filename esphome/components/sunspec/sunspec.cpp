#include "sunspec.h"
#include "esphome/core/controller_registry.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace sunspec {

static const char *const TAG = "sunspec";

void SunSpecServer::setup() {
  ControllerRegistry::register_controller(this);

  if (this->controls_enabled_) {
    this->terminator_offset_ = MODEL_123_DATA_OFFSET + MODEL_123_LENGTH;
  } else {
    this->terminator_offset_ = MODEL_123_OFFSET;
  }
  this->total_registers_ = this->terminator_offset_ + 2;

  this->build_points_();
  this->build_register_map_();

  if (!this->tcp_.setup()) {
    this->mark_failed();
    return;
  }
  this->tcp_.register_device(this);
}

void SunSpecServer::loop() {
  this->tcp_.loop();
  this->check_stale_points_();
}

void SunSpecServer::dump_config() {
  ESP_LOGCONFIG(TAG,
                "SunSpec Server:\n"
                "  Port: %u\n"
                "  Max Connections: %u\n"
                "  Base Address: %u (0x%04X)\n"
                "  Unit Address: 0x%02X\n"
                "  Stale Timeout: %u ms\n"
                "  Manufacturer: %s\n"
                "  Model: %s\n"
                "  Serial Number: %s\n"
                "  Version: %s\n"
                "  Inverter Model: %u (%u sensors)\n"
                "  Immediate Controls (Model 123): %s",
                this->tcp_.get_port(), this->tcp_.get_max_connections(), this->base_address_, this->base_address_,
                this->address_, this->stale_timeout_ms_, this->manufacturer_.c_str(), this->model_.c_str(),
                this->serial_number_.c_str(), this->version_.c_str(), this->model_id_, this->points_.size(),
                YESNO(this->controls_enabled_));
}

void SunSpecServer::build_points_() {
  auto add = [this](sensor::Sensor *s, uint16_t reg, uint16_t mirror, PointType type, int16_t sf) {
    if (s != nullptr) {
      this->points_.push_back(Point{s, reg, mirror, type, sf});
    }
  };
  bool single_phase = this->model_id_ == 101;

  add(this->ac_power_sensor_, REG_AC_POWER, 0, PointType::TYPE_INT16, SF_POWER);
  // Single phase: AphA mirrors the total current A
  add(this->ac_current_sensor_, REG_AC_CURRENT, single_phase ? REG_AC_CURRENT_PHA : 0, PointType::TYPE_UINT16,
      SF_CURRENT);
  add(this->ac_current_phase_[0], REG_AC_CURRENT_PHA, 0, PointType::TYPE_UINT16, SF_CURRENT);
  add(this->ac_current_phase_[1], REG_AC_CURRENT_PHB, 0, PointType::TYPE_UINT16, SF_CURRENT);
  add(this->ac_current_phase_[2], REG_AC_CURRENT_PHC, 0, PointType::TYPE_UINT16, SF_CURRENT);
  add(this->ac_voltage_sensor_, REG_AC_VOLTAGE_PHA, 0, PointType::TYPE_UINT16, SF_VOLTAGE);
  add(this->ac_voltage_phase_[0], REG_AC_VOLTAGE_PHA, 0, PointType::TYPE_UINT16, SF_VOLTAGE);
  add(this->ac_voltage_phase_[1], REG_AC_VOLTAGE_PHB, 0, PointType::TYPE_UINT16, SF_VOLTAGE);
  add(this->ac_voltage_phase_[2], REG_AC_VOLTAGE_PHC, 0, PointType::TYPE_UINT16, SF_VOLTAGE);
  add(this->ac_frequency_sensor_, REG_AC_FREQUENCY, 0, PointType::TYPE_UINT16, SF_FREQUENCY);
  add(this->energy_sensor_, REG_ENERGY, 0, PointType::TYPE_ACC32, 0);
  add(this->dc_current_sensor_, REG_DC_CURRENT, 0, PointType::TYPE_UINT16, SF_CURRENT);
  add(this->dc_voltage_sensor_, REG_DC_VOLTAGE, 0, PointType::TYPE_UINT16, SF_VOLTAGE);
  add(this->dc_power_sensor_, REG_DC_POWER, 0, PointType::TYPE_INT16, SF_POWER);
  add(this->temperature_sensor_, REG_TEMPERATURE, 0, PointType::TYPE_INT16, SF_TEMPERATURE);
}

void SunSpecServer::build_register_map_() {
  auto &regs = this->registers_;
  auto sf = [](int16_t v) { return static_cast<uint16_t>(v); };

  regs.fill(0);

  // SunSpec ID ("SunS")
  regs[SUNSPEC_ID_OFFSET] = 0x5375;      // "Su"
  regs[SUNSPEC_ID_OFFSET + 1] = 0x6e53;  // "nS"

  // Model 1 - Common: Mn(16) Md(16) Opt(8) Vr(8) SN(16) DA(1) Pad(1) = 66 regs
  regs[MODEL_1_OFFSET] = 1;
  regs[MODEL_1_OFFSET + 1] = MODEL_1_LENGTH;
  this->write_string_(MODEL_1_DATA_OFFSET + 0, this->manufacturer_, 16);
  this->write_string_(MODEL_1_DATA_OFFSET + 16, this->model_, 16);
  // Options (offset 32, 8 regs) - not used, left 0
  this->write_string_(MODEL_1_DATA_OFFSET + 40, this->version_, 8);
  this->write_string_(MODEL_1_DATA_OFFSET + 48, this->serial_number_, 16);
  regs[MODEL_1_DATA_OFFSET + 64] = this->address_;  // DA
  // Pad (offset 65) - left 0

  // Model 101/103 - Inverter
  regs[MODEL_10X_OFFSET] = this->model_id_;
  regs[MODEL_10X_OFFSET + 1] = MODEL_10X_LENGTH;

  bool has_ac_current = this->ac_current_sensor_ || this->ac_current_phase_[0] || this->ac_current_phase_[1] ||
                        this->ac_current_phase_[2];
  bool has_ac_voltage = this->ac_voltage_sensor_ || this->ac_voltage_phase_[0] || this->ac_voltage_phase_[1] ||
                        this->ac_voltage_phase_[2];

  // Value points start as "not implemented"; on_sensor_update() fills the
  // configured ones. Scale factors are NI unless their point has a sensor.
  regs[REG_AC_CURRENT] = SUNSPEC_UINT16_NI;      // A
  regs[REG_AC_CURRENT_PHA] = SUNSPEC_UINT16_NI;  // AphA
  regs[REG_AC_CURRENT_PHB] = SUNSPEC_UINT16_NI;  // AphB
  regs[REG_AC_CURRENT_PHC] = SUNSPEC_UINT16_NI;  // AphC
  regs[REG_AC_CURRENT_SF] = has_ac_current ? sf(SF_CURRENT) : sf(SUNSPEC_INT16_NI);
  for (uint16_t i = 5; i <= 10; i++) {
    regs[MODEL_10X_DATA_OFFSET + i] = SUNSPEC_UINT16_NI;  // PPVphAB..PhVphC
  }
  regs[REG_AC_VOLTAGE_SF] = has_ac_voltage ? sf(SF_VOLTAGE) : sf(SUNSPEC_INT16_NI);
  regs[REG_AC_POWER] = sf(SUNSPEC_INT16_NI);  // W
  regs[REG_AC_POWER_SF] = this->ac_power_sensor_ ? sf(SF_POWER) : sf(SUNSPEC_INT16_NI);
  regs[REG_AC_FREQUENCY] = SUNSPEC_UINT16_NI;  // Hz
  regs[REG_AC_FREQUENCY_SF] = this->ac_frequency_sensor_ ? sf(SF_FREQUENCY) : sf(SUNSPEC_INT16_NI);
  regs[MODEL_10X_DATA_OFFSET + 16] = sf(SUNSPEC_INT16_NI);  // VA
  regs[MODEL_10X_DATA_OFFSET + 17] = sf(SUNSPEC_INT16_NI);  // VA_SF
  regs[MODEL_10X_DATA_OFFSET + 18] = sf(SUNSPEC_INT16_NI);  // VAr
  regs[MODEL_10X_DATA_OFFSET + 19] = sf(SUNSPEC_INT16_NI);  // VAr_SF
  regs[MODEL_10X_DATA_OFFSET + 20] = sf(SUNSPEC_INT16_NI);  // PF
  regs[MODEL_10X_DATA_OFFSET + 21] = sf(SUNSPEC_INT16_NI);  // PF_SF
  // WH (acc32, offsets 22-23): 0 = not accumulated; WH_SF (24): 0
  regs[REG_DC_CURRENT] = SUNSPEC_UINT16_NI;
  regs[REG_DC_CURRENT_SF] = this->dc_current_sensor_ ? sf(SF_CURRENT) : sf(SUNSPEC_INT16_NI);
  regs[REG_DC_VOLTAGE] = SUNSPEC_UINT16_NI;
  regs[REG_DC_VOLTAGE_SF] = this->dc_voltage_sensor_ ? sf(SF_VOLTAGE) : sf(SUNSPEC_INT16_NI);
  regs[REG_DC_POWER] = sf(SUNSPEC_INT16_NI);
  regs[REG_DC_POWER_SF] = this->dc_power_sensor_ ? sf(SF_POWER) : sf(SUNSPEC_INT16_NI);
  regs[REG_TEMPERATURE] = sf(SUNSPEC_INT16_NI);              // TmpCab
  regs[MODEL_10X_DATA_OFFSET + 32] = sf(SUNSPEC_INT16_NI);   // TmpSnk
  regs[MODEL_10X_DATA_OFFSET + 33] = sf(SUNSPEC_INT16_NI);   // TmpTrns
  regs[MODEL_10X_DATA_OFFSET + 34] = sf(SUNSPEC_INT16_NI);   // TmpOt
  regs[REG_TEMPERATURE_SF] = this->temperature_sensor_ ? sf(SF_TEMPERATURE) : sf(SUNSPEC_INT16_NI);
  // St: assume producing until the AC power sensor tells us otherwise
  regs[REG_STATE] = SUNSPEC_ST_MPPT;
  regs[REG_STATE_VENDOR] = SUNSPEC_UINT16_NI;  // StVnd: not implemented
  // Evt1/Evt2 (offsets 38-41): 0 = no events; EvtVnd1-4 (42-49): left 0

  // Model 123 - Immediate Controls (optional)
  if (this->controls_enabled_) {
    regs[MODEL_123_OFFSET] = 123;
    regs[MODEL_123_OFFSET + 1] = MODEL_123_LENGTH;
    regs[MODEL_123_DATA_OFFSET + 0] = SUNSPEC_UINT16_NI;      // Conn_WinTms
    regs[MODEL_123_DATA_OFFSET + 1] = SUNSPEC_UINT16_NI;      // Conn_RvrtTms
    regs[REG_CONN] = 1;                                       // Conn: connected
    regs[REG_WMAXLIMPCT] = 10000;                             // WMaxLimPct: 100.00 %
    regs[MODEL_123_DATA_OFFSET + 4] = SUNSPEC_UINT16_NI;      // WMaxLimPct_WinTms
    regs[MODEL_123_DATA_OFFSET + 5] = SUNSPEC_UINT16_NI;      // WMaxLimPct_RvrtTms
    regs[MODEL_123_DATA_OFFSET + 6] = SUNSPEC_UINT16_NI;      // WMaxLimPct_RmpTms
    regs[REG_WMAXLIM_ENA] = 0;                                // WMaxLim_Ena: disabled
    regs[MODEL_123_DATA_OFFSET + 8] = sf(SUNSPEC_INT16_NI);   // OutPFSet
    regs[MODEL_123_DATA_OFFSET + 9] = SUNSPEC_UINT16_NI;      // OutPFSet_WinTms
    regs[MODEL_123_DATA_OFFSET + 10] = SUNSPEC_UINT16_NI;     // OutPFSet_RvrtTms
    regs[MODEL_123_DATA_OFFSET + 11] = SUNSPEC_UINT16_NI;     // OutPFSet_RmpTms
    regs[MODEL_123_DATA_OFFSET + 12] = 0;                     // OutPFSet_Ena: disabled
    regs[MODEL_123_DATA_OFFSET + 13] = sf(SUNSPEC_INT16_NI);  // VArWMaxPct
    regs[MODEL_123_DATA_OFFSET + 14] = sf(SUNSPEC_INT16_NI);  // VArMaxPct
    regs[MODEL_123_DATA_OFFSET + 15] = sf(SUNSPEC_INT16_NI);  // VArAvalPct
    regs[MODEL_123_DATA_OFFSET + 16] = SUNSPEC_UINT16_NI;     // VArPct_WinTms
    regs[MODEL_123_DATA_OFFSET + 17] = SUNSPEC_UINT16_NI;     // VArPct_RvrtTms
    regs[MODEL_123_DATA_OFFSET + 18] = SUNSPEC_UINT16_NI;     // VArPct_RmpTms
    regs[MODEL_123_DATA_OFFSET + 19] = SUNSPEC_UINT16_NI;     // VArPct_Mod
    regs[MODEL_123_DATA_OFFSET + 20] = 0;                     // VArPct_Ena: disabled
    regs[MODEL_123_DATA_OFFSET + 21] = sf(-2);                // WMaxLimPct_SF: 0.01 %
    regs[MODEL_123_DATA_OFFSET + 22] = sf(SUNSPEC_INT16_NI);  // OutPFSet_SF
    regs[MODEL_123_DATA_OFFSET + 23] = sf(SUNSPEC_INT16_NI);  // VArPct_SF
  }

  // Terminator model (ID 0xFFFF, length 0)
  regs[this->terminator_offset_] = 0xFFFF;
  regs[this->terminator_offset_ + 1] = 0;
}

void SunSpecServer::on_sensor_update(sensor::Sensor *obj) {
  if (!obj->has_state())
    return;

  for (auto &point : this->points_) {
    if (point.sensor != obj)
      continue;

    if (point.type == PointType::TYPE_ACC32) {
      uint32_t value = obj->state < 0 ? 0 : static_cast<uint32_t>(std::round(obj->state));
      this->registers_[point.reg] = value >> 16;
      this->registers_[point.reg + 1] = value & 0xFFFF;
    } else {
      uint16_t raw = point.type == PointType::TYPE_INT16 ? scale_int16_(obj->state, point.scale_factor)
                                                         : scale_uint16_(obj->state, point.scale_factor);
      this->registers_[point.reg] = raw;
      if (point.mirror_reg != 0) {
        this->registers_[point.mirror_reg] = raw;
      }
    }
    point.last_update = millis();
    point.has_value = true;

    // Derive operating state from AC power: producing -> MPPT, else SLEEPING
    if (obj == this->ac_power_sensor_) {
      this->registers_[REG_STATE] = obj->state < 1.0f ? SUNSPEC_ST_SLEEPING : SUNSPEC_ST_MPPT;
    }
  }
}

void SunSpecServer::check_stale_points_() {
  if (this->stale_timeout_ms_ == 0)
    return;
  uint32_t now = millis();
  if (now - this->last_stale_check_ < 1000)
    return;
  this->last_stale_check_ = now;

  for (auto &point : this->points_) {
    // Energy is an accumulator; the last known total stays valid
    if (!point.has_value || point.type == PointType::TYPE_ACC32)
      continue;
    if (now - point.last_update < this->stale_timeout_ms_)
      continue;

    uint16_t ni = point.type == PointType::TYPE_INT16 ? static_cast<uint16_t>(SUNSPEC_INT16_NI) : SUNSPEC_UINT16_NI;
    this->registers_[point.reg] = ni;
    if (point.mirror_reg != 0) {
      this->registers_[point.mirror_reg] = ni;
    }
    point.has_value = false;

    if (point.sensor == this->ac_power_sensor_) {
      this->registers_[REG_STATE] = SUNSPEC_ST_OFF;
    }
    ESP_LOGW(TAG, "Sensor '%s' has not updated for %u ms; reporting point as not implemented",
             point.sensor->get_name().c_str(), this->stale_timeout_ms_);
  }
}

void SunSpecServer::on_modbus_read_registers(uint8_t function_code, uint16_t start_address,
                                             uint16_t number_of_registers) {
  ESP_LOGD(TAG, "Read registers request: addr=0x%04X, count=%d", start_address, number_of_registers);

  // Validate range
  if (number_of_registers == 0 || number_of_registers > 125) {
    this->send_error(function_code, modbus::ModbusExceptionCode::ILLEGAL_DATA_VALUE);
    return;
  }

  // Check if request is within SunSpec range
  if (start_address < this->base_address_ ||
      start_address + number_of_registers > this->base_address_ + this->total_registers_) {
    this->send_error(function_code, modbus::ModbusExceptionCode::ILLEGAL_DATA_ADDRESS);
    return;
  }

  this->send_read_response_(function_code, start_address, number_of_registers);
}

void SunSpecServer::on_modbus_write_registers(uint8_t function_code, uint16_t start_address,
                                              const std::vector<uint16_t> &values) {
  ESP_LOGD(TAG, "Write registers request: addr=0x%04X, count=%u", start_address, values.size());

  // Only the writable part of Model 123 accepts writes (scale factors are read-only)
  uint16_t writable_start = this->base_address_ + MODEL_123_DATA_OFFSET;
  if (!this->controls_enabled_ || start_address < writable_start ||
      start_address + values.size() > writable_start + MODEL_123_WRITABLE) {
    this->send_error(function_code, modbus::ModbusExceptionCode::ILLEGAL_DATA_ADDRESS);
    return;
  }

  uint16_t offset = start_address - this->base_address_;
  bool limit_changed = false;
  for (size_t i = 0; i < values.size(); i++) {
    this->registers_[offset + i] = values[i];
    if (offset + i == REG_WMAXLIMPCT || offset + i == REG_WMAXLIM_ENA) {
      limit_changed = true;
    }
  }

  // Success response: FC 0x06 echoes address + value, FC 0x10 echoes address + count
  std::vector<uint8_t> response;
  response.push_back(function_code);
  response.push_back(start_address >> 8);
  response.push_back(start_address & 0xFF);
  if (function_code == 0x06) {
    response.push_back(values[0] >> 8);
    response.push_back(values[0] & 0xFF);
  } else {
    response.push_back(values.size() >> 8);
    response.push_back(values.size() & 0xFF);
  }
  this->tcp_.send_response(this->tcp_.get_active_conn_id(), this->tcp_.get_active_transaction_id(),
                           this->tcp_.get_active_unit_id(), response);

  if (limit_changed) {
    float pct = this->get_power_limit_pct();
    bool enabled = this->get_power_limit_enabled();
    ESP_LOGI(TAG, "Power limit command: %.2f%% (%s)", pct, enabled ? "enabled" : "disabled");
    this->power_limit_callback_.call(pct, enabled);
  }
}

void SunSpecServer::send_read_response_(uint8_t function_code, uint16_t start_address, uint16_t count) {
  std::vector<uint8_t> response;
  response.reserve(1 + 1 + count * 2);  // FC + byte count + data

  response.push_back(function_code);
  response.push_back(count * 2);  // Byte count

  uint16_t offset = start_address - this->base_address_;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t reg_value = this->registers_[offset + i];
    response.push_back(reg_value >> 8);    // High byte
    response.push_back(reg_value & 0xFF);  // Low byte
  }

  // Send response via TCP connection using active connection context
  this->tcp_.send_response(this->tcp_.get_active_conn_id(), this->tcp_.get_active_transaction_id(),
                           this->tcp_.get_active_unit_id(), response);
}

uint16_t SunSpecServer::scale_int16_(float value, int16_t scale_factor) {
  // register_value = actual_value / 10^scale_factor
  float scaled = value / std::pow(10.0f, static_cast<float>(scale_factor));

  if (scaled > 32767.0f || scaled < -32768.0f) {
    ESP_LOGW(TAG, "Value %.1f does not fit register with scale factor %d; clamping", value, scale_factor);
    return scaled > 0 ? static_cast<uint16_t>(32767) : static_cast<uint16_t>(static_cast<int16_t>(-32768));
  }

  return static_cast<uint16_t>(static_cast<int16_t>(std::round(scaled)));
}

uint16_t SunSpecServer::scale_uint16_(float value, int16_t scale_factor) {
  float scaled = value / std::pow(10.0f, static_cast<float>(scale_factor));

  // Cap below the 0xFFFF "not implemented" sentinel
  if (scaled < 0.0f || scaled > 65534.0f) {
    ESP_LOGW(TAG, "Value %.1f does not fit register with scale factor %d; clamping", value, scale_factor);
    return scaled < 0.0f ? 0 : 65534;
  }

  return static_cast<uint16_t>(std::round(scaled));
}

void SunSpecServer::write_string_(uint16_t offset, const std::string &str, size_t max_regs) {
  // Big-endian, 2 characters per register; unused registers stay 0
  size_t len = std::min(str.length(), max_regs * 2);
  for (size_t i = 0; i < len; i += 2) {
    uint16_t reg_value = static_cast<uint16_t>(str[i]) << 8;
    if (i + 1 < len) {
      reg_value |= static_cast<uint16_t>(str[i + 1]);
    }
    this->registers_[offset + i / 2] = reg_value;
  }
}

}  // namespace sunspec
}  // namespace esphome
