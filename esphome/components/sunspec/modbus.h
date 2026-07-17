#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome {
namespace sunspec {

namespace modbus_tcp {
class ModbusTCP;
}  // namespace modbus_tcp

namespace modbus {

/// Modbus exception codes
enum class ModbusExceptionCode : uint8_t {
  ILLEGAL_FUNCTION = 0x01,
  ILLEGAL_DATA_ADDRESS = 0x02,
  ILLEGAL_DATA_VALUE = 0x03,
  SERVER_DEVICE_FAILURE = 0x04,
  GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND = 0x0B,
};

/// Base class for Modbus devices
class ModbusDevice {
 public:
  virtual ~ModbusDevice() = default;

  /// Called when a read registers request is received
  virtual void on_modbus_read_registers(uint8_t function_code, uint16_t start_address,
                                        uint16_t number_of_registers) = 0;

  /// Called when a write registers request (FC 0x06 / 0x10) is received.
  /// The device is responsible for sending the response or an exception.
  virtual void on_modbus_write_registers(uint8_t function_code, uint16_t start_address,
                                         const std::vector<uint16_t> &values) = 0;

  /// Send an error response
  void send_error(uint8_t function_code, ModbusExceptionCode exception_code);

  /// Set the active connection context (called by ModbusTCP before invoking callbacks)
  void set_active_context(modbus_tcp::ModbusTCP *tcp_server, size_t conn_id, uint16_t transaction_id,
                          uint8_t unit_id) {
    this->active_tcp_server_ = tcp_server;
    this->active_conn_id_ = conn_id;
    this->active_transaction_id_ = transaction_id;
    this->active_unit_id_ = unit_id;
  }

  uint8_t address_{1};  ///< Modbus unit address

 protected:
  modbus_tcp::ModbusTCP *active_tcp_server_{nullptr};
  size_t active_conn_id_{0};
  uint16_t active_transaction_id_{0};
  uint8_t active_unit_id_{0};
};

}  // namespace modbus
}  // namespace sunspec
}  // namespace esphome
