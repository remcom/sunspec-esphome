#include "sunspec.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstring>

namespace esphome {
namespace sunspec {

static const char *const TAG = "sunspec";

SunSpecServer::SunSpecServer() = default;

void SunSpecServer::setup() {
  ESP_LOGCONFIG(TAG, "Setting up SunSpec server...");

  // Setup the embedded ModbusTCP server
  this->tcp_.setup();

  // Register this device with the ModbusTCP server
  this->address_ = this->unit_address_;
  this->tcp_.register_device(this);
  ESP_LOGD(TAG, "Registered with Modbus TCP server at unit address 0x%02X", this->address_);

  ESP_LOGD(TAG, "SunSpec server initialized at base address %d (0x%04X)", this->base_address_, this->base_address_);
}

void SunSpecServer::loop() {
  // Run the ModbusTCP server loop
  this->tcp_.loop();
}

void SunSpecServer::dump_config() {
  ESP_LOGCONFIG(TAG, "SunSpec Server:");
  ESP_LOGCONFIG(TAG, "  Base Address: %d (0x%04X)", this->base_address_, this->base_address_);
  ESP_LOGCONFIG(TAG, "  Unit Address: 0x%02X", this->unit_address_);
  ESP_LOGCONFIG(TAG, "  Manufacturer: %s", this->manufacturer_.c_str());
  ESP_LOGCONFIG(TAG, "  Model: %s", this->model_.c_str());
  ESP_LOGCONFIG(TAG, "  Serial Number: %s", this->serial_number_.c_str());
  ESP_LOGCONFIG(TAG, "  Version: %s", this->version_.c_str());

  if (this->ac_power_sensor_ || this->ac_voltage_sensor_ || this->ac_current_sensor_ || this->ac_frequency_sensor_ ||
      this->dc_power_sensor_ || this->dc_voltage_sensor_ || this->dc_current_sensor_ || this->temperature_sensor_) {
    ESP_LOGCONFIG(TAG, "  Model 101 (Single Phase Inverter) enabled:");
    ESP_LOGCONFIG(TAG, "    AC Power: %s", YESNO(this->ac_power_sensor_));
    ESP_LOGCONFIG(TAG, "    AC Voltage: %s", YESNO(this->ac_voltage_sensor_));
    ESP_LOGCONFIG(TAG, "    AC Current: %s", YESNO(this->ac_current_sensor_));
    ESP_LOGCONFIG(TAG, "    AC Frequency: %s", YESNO(this->ac_frequency_sensor_));
    ESP_LOGCONFIG(TAG, "    DC Power: %s", YESNO(this->dc_power_sensor_));
    ESP_LOGCONFIG(TAG, "    DC Voltage: %s", YESNO(this->dc_voltage_sensor_));
    ESP_LOGCONFIG(TAG, "    DC Current: %s", YESNO(this->dc_current_sensor_));
    ESP_LOGCONFIG(TAG, "    Temperature: %s", YESNO(this->temperature_sensor_));
  }
}

float SunSpecServer::get_setup_priority() const { return setup_priority::DATA; }

void SunSpecServer::on_sensor_update(sensor::Sensor *obj) {
  // Update cached scaled values when sensors change
  if (obj == this->ac_power_sensor_ && obj->has_state()) {
    // AC Power in W, scale factor 0
    this->ac_power_scaled_ = this->scale_int16_(obj->state, 0);
  } else if (obj == this->ac_voltage_sensor_ && obj->has_state()) {
    // AC Voltage in V, scale factor -1 (0.1V precision)
    this->ac_voltage_scaled_ = this->scale_uint16_(obj->state, -1);
  } else if (obj == this->ac_current_sensor_ && obj->has_state()) {
    // AC Current in A, scale factor -2 (0.01A precision)
    this->ac_current_scaled_ = this->scale_uint16_(obj->state, -2);
  } else if (obj == this->ac_frequency_sensor_ && obj->has_state()) {
    // AC Frequency in Hz, scale factor -2 (0.01Hz precision)
    this->ac_frequency_scaled_ = this->scale_uint16_(obj->state, -2);
  } else if (obj == this->dc_power_sensor_ && obj->has_state()) {
    // DC Power in W, scale factor 0
    this->dc_power_scaled_ = this->scale_int16_(obj->state, 0);
  } else if (obj == this->dc_voltage_sensor_ && obj->has_state()) {
    // DC Voltage in V, scale factor -1 (0.1V precision)
    this->dc_voltage_scaled_ = this->scale_uint16_(obj->state, -1);
  } else if (obj == this->dc_current_sensor_ && obj->has_state()) {
    // DC Current in A, scale factor -2 (0.01A precision)
    this->dc_current_scaled_ = this->scale_uint16_(obj->state, -2);
  } else if (obj == this->temperature_sensor_ && obj->has_state()) {
    // Temperature in °C, scale factor 0
    this->temperature_scaled_ = this->scale_int16_(obj->state, 0);
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
      start_address + number_of_registers > this->base_address_ + TERMINATOR_OFFSET + 2) {
    this->send_error(function_code, modbus::ModbusExceptionCode::ILLEGAL_DATA_ADDRESS);
    return;
  }

  this->send_read_response_(function_code, start_address, number_of_registers);
}

void SunSpecServer::send_read_response_(uint8_t function_code, uint16_t start_address, uint16_t count) {
  // Build response
  std::vector<uint8_t> response;
  response.reserve(1 + 1 + count * 2);  // FC + byte count + data

  response.push_back(function_code);
  response.push_back(count * 2);  // Byte count

  // Read each register
  for (uint16_t i = 0; i < count; i++) {
    uint16_t reg_value = this->read_register_(start_address + i);
    response.push_back(reg_value >> 8);    // High byte
    response.push_back(reg_value & 0xFF);  // Low byte
  }

  // Send response via TCP connection using active connection context
  size_t conn_id = this->tcp_.get_active_conn_id();
  uint16_t transaction_id = this->tcp_.get_active_transaction_id();
  uint8_t unit_id = this->tcp_.get_active_unit_id();
  this->tcp_.send_response(conn_id, transaction_id, unit_id, response);
}

uint16_t SunSpecServer::read_register_(uint16_t address) {
  // Convert absolute address to offset
  uint16_t offset = address - this->base_address_;

  // SunSpec ID ("SunS" = 0x5375 0x6e53)
  if (offset == SUNSPEC_ID_OFFSET) {
    return 0x5375;  // "Su"
  }
  if (offset == SUNSPEC_ID_OFFSET + 1) {
    return 0x6e53;  // "nS"
  }

  // Model 1 - Common
  if (offset == MODEL_1_OFFSET) {
    return 1;  // Model ID
  }
  if (offset == MODEL_1_OFFSET + 1) {
    return MODEL_1_LENGTH;  // Model length
  }

  // Model 1 data
  if (offset >= MODEL_1_DATA_OFFSET && offset < MODEL_1_DATA_OFFSET + MODEL_1_LENGTH) {
    uint16_t data_offset = offset - MODEL_1_DATA_OFFSET;

    // Manufacturer (32 registers = 64 characters)
    if (data_offset < 32) {
      uint16_t regs[32] = {0};
      this->string_to_registers_(this->manufacturer_, regs, 32);
      return regs[data_offset];
    }
    data_offset -= 32;

    // Model (16 registers = 32 characters)
    if (data_offset < 16) {
      uint16_t regs[16] = {0};
      this->string_to_registers_(this->model_, regs, 16);
      return regs[data_offset];
    }
    data_offset -= 16;

    // Options (8 registers = 16 characters) - not used
    if (data_offset < 8) {
      return 0;
    }
    data_offset -= 8;

    // Version (8 registers = 16 characters)
    if (data_offset < 8) {
      uint16_t regs[8] = {0};
      this->string_to_registers_(this->version_, regs, 8);
      return regs[data_offset];
    }
    data_offset -= 8;

    // Serial Number (16 registers = 32 characters)
    if (data_offset < 16) {
      uint16_t regs[16] = {0};
      this->string_to_registers_(this->serial_number_, regs, 16);
      return regs[data_offset];
    }
    data_offset -= 16;

    // Device Address (1 register)
    if (data_offset == 0) {
      return this->unit_address_;
    }

    // Pad (1 register)
    return 0;
  }

  // Model 101 - Single Phase Inverter
  if (offset == MODEL_101_OFFSET) {
    return 101;  // Model ID
  }
  if (offset == MODEL_101_OFFSET + 1) {
    return MODEL_101_LENGTH;  // Model length
  }

  // Model 101 data
  if (offset >= MODEL_101_DATA_OFFSET && offset < MODEL_101_DATA_OFFSET + MODEL_101_LENGTH) {
    uint16_t data_offset = offset - MODEL_101_DATA_OFFSET;

    // AC Current (1 register, uint16)
    if (data_offset == 0) {
      return this->ac_current_scaled_;
    }
    // AC Current Scale Factor (1 register, int16)
    if (data_offset == 1) {
      return static_cast<uint16_t>(static_cast<int16_t>(-2));  // 0.01A
    }
    // AC Voltage AN (1 register, uint16)
    if (data_offset == 2) {
      return this->ac_voltage_scaled_;
    }
    // AC Voltage Scale Factor (1 register, int16)
    if (data_offset == 3) {
      return static_cast<uint16_t>(static_cast<int16_t>(-1));  // 0.1V
    }
    // AC Power (1 register, int16)
    if (data_offset == 4) {
      return static_cast<uint16_t>(this->ac_power_scaled_);
    }
    // AC Power Scale Factor (1 register, int16)
    if (data_offset == 5) {
      return static_cast<uint16_t>(static_cast<int16_t>(0));  // 1W
    }
    // AC Frequency (1 register, uint16)
    if (data_offset == 6) {
      return this->ac_frequency_scaled_;
    }
    // AC Frequency Scale Factor (1 register, int16)
    if (data_offset == 7) {
      return static_cast<uint16_t>(static_cast<int16_t>(-2));  // 0.01Hz
    }
    // AC VA (apparent power) - not implemented
    if (data_offset == 8) {
      return SUNSPEC_INT16_NI;
    }
    // AC VA Scale Factor
    if (data_offset == 9) {
      return static_cast<uint16_t>(static_cast<int16_t>(0));
    }
    // AC VAR (reactive power) - not implemented
    if (data_offset == 10) {
      return SUNSPEC_INT16_NI;
    }
    // AC VAR Scale Factor
    if (data_offset == 11) {
      return static_cast<uint16_t>(static_cast<int16_t>(0));
    }
    // AC PF (power factor) - not implemented
    if (data_offset == 12) {
      return SUNSPEC_INT16_NI;
    }
    // AC PF Scale Factor
    if (data_offset == 13) {
      return static_cast<uint16_t>(static_cast<int16_t>(-2));
    }
    // AC Energy WH (lifetime energy) - not implemented for now
    if (data_offset == 14 || data_offset == 15) {
      return 0;  // uint32 accumulator
    }
    // AC Energy Scale Factor
    if (data_offset == 16) {
      return static_cast<uint16_t>(static_cast<int16_t>(0));
    }
    // DC Current (1 register, uint16)
    if (data_offset == 17) {
      return this->dc_current_scaled_;
    }
    // DC Current Scale Factor
    if (data_offset == 18) {
      return static_cast<uint16_t>(static_cast<int16_t>(-2));  // 0.01A
    }
    // DC Voltage (1 register, uint16)
    if (data_offset == 19) {
      return this->dc_voltage_scaled_;
    }
    // DC Voltage Scale Factor
    if (data_offset == 20) {
      return static_cast<uint16_t>(static_cast<int16_t>(-1));  // 0.1V
    }
    // DC Power (1 register, int16)
    if (data_offset == 21) {
      return static_cast<uint16_t>(this->dc_power_scaled_);
    }
    // DC Power Scale Factor
    if (data_offset == 22) {
      return static_cast<uint16_t>(static_cast<int16_t>(0));  // 1W
    }
    // Cabinet Temperature (1 register, int16)
    if (data_offset == 23) {
      return static_cast<uint16_t>(this->temperature_scaled_);
    }
    // Temperature Scale Factor
    if (data_offset == 24) {
      return static_cast<uint16_t>(static_cast<int16_t>(0));  // 1°C
    }
    // Operating State (1 register, enum16)
    if (data_offset == 25) {
      // 4 = MPPT (producing power)
      return 4;
    }
    // Vendor Operating State (1 register, enum16)
    if (data_offset == 26) {
      return 0;  // Not used
    }
    // Event1 through Event2 (4 registers, bitfield32) - no events
    if (data_offset >= 27 && data_offset <= 30) {
      return 0;
    }
    // Vendor Event1 through Vendor Event4 (8 registers, bitfield32) - not used
    if (data_offset >= 31 && data_offset <= 38) {
      return 0;
    }

    // Remaining registers are vendor-specific or padding
    return 0;
  }

  // Terminator
  if (offset == TERMINATOR_OFFSET) {
    return 0xFFFF;  // End marker
  }
  if (offset == TERMINATOR_OFFSET + 1) {
    return 0;  // Length = 0
  }

  // Outside of known range
  return 0;
}

int16_t SunSpecServer::scale_int16_(float value, int16_t scale_factor) {
  // Apply scale factor: actual_value = register_value * 10^scale_factor
  // So: register_value = actual_value / 10^scale_factor
  float scale = std::pow(10.0f, static_cast<float>(scale_factor));
  float scaled = value / scale;

  // Clamp to int16 range
  if (scaled > 32767.0f) {
    return 32767;
  }
  if (scaled < -32768.0f) {
    return -32768;
  }

  return static_cast<int16_t>(std::round(scaled));
}

uint16_t SunSpecServer::scale_uint16_(float value, int16_t scale_factor) {
  // Apply scale factor
  float scale = std::pow(10.0f, static_cast<float>(scale_factor));
  float scaled = value / scale;

  // Clamp to uint16 range
  if (scaled < 0.0f) {
    return 0;
  }
  if (scaled > 65535.0f) {
    return 65535;
  }

  return static_cast<uint16_t>(std::round(scaled));
}

void SunSpecServer::string_to_registers_(const std::string &str, uint16_t *regs, size_t max_regs) {
  // Convert string to registers (big-endian, 2 characters per register)
  // Pad with zeros if string is shorter than max length
  size_t max_chars = max_regs * 2;
  size_t len = std::min(str.length(), max_chars);

  std::memset(regs, 0, max_regs * sizeof(uint16_t));

  for (size_t i = 0; i < len; i += 2) {
    uint16_t reg_value = 0;
    reg_value = static_cast<uint16_t>(str[i]) << 8;
    if (i + 1 < len) {
      reg_value |= static_cast<uint16_t>(str[i + 1]);
    }
    regs[i / 2] = reg_value;
  }
}

}  // namespace sunspec
}  // namespace esphome
