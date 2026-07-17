#pragma once

#include "modbus.h"

#include <vector>

namespace esphome {
namespace sunspec {
namespace modbus_tcp {

struct ClientConnection {
  int socket{-1};
  bool active{false};
  uint32_t last_activity{0};
};

/// Modbus TCP server (ESP32 / lwip only)
class ModbusTCP {
 public:
  ~ModbusTCP();

  /// Create the listening socket. Returns false on failure.
  bool setup();
  void loop();

  /// Set the TCP port to listen on
  void set_port(uint16_t port) { this->port_ = port; }

  /// Set the maximum number of concurrent connections
  void set_max_connections(uint8_t max_connections) { this->max_connections_ = max_connections; }

  uint16_t get_port() const { return this->port_; }
  uint8_t get_max_connections() const { return this->max_connections_; }

  /// Number of currently connected clients
  uint8_t get_client_count() const;

  /// Register a Modbus device
  void register_device(modbus::ModbusDevice *device);

  /// Send a response to a client
  void send_response(size_t conn_id, uint16_t transaction_id, uint8_t unit_id, const std::vector<uint8_t> &data);

  /// Get active connection context (for use by devices during callbacks)
  size_t get_active_conn_id() const { return this->active_conn_id_; }
  uint16_t get_active_transaction_id() const { return this->active_transaction_id_; }
  uint8_t get_active_unit_id() const { return this->active_unit_id_; }

 protected:
  void accept_connection_();
  void handle_client_(size_t conn_id);
  void close_connection_(size_t conn_id);
  void process_modbus_request_(size_t conn_id, uint16_t transaction_id, uint8_t unit_id, uint8_t function_code,
                               const std::vector<uint8_t> &data);
  void send_exception_(size_t conn_id, uint16_t transaction_id, uint8_t unit_id, uint8_t function_code,
                       modbus::ModbusExceptionCode exception_code);

  // Connections silent for longer than this are closed to free up slots
  static constexpr uint32_t IDLE_TIMEOUT_MS = 300000;  // 5 minutes

  uint16_t port_{502};
  uint8_t max_connections_{4};
  int server_socket_{-1};
  std::vector<ClientConnection> connections_;
  std::vector<modbus::ModbusDevice *> devices_;

  // Active request context (set before calling device callbacks)
  size_t active_conn_id_{0};
  uint16_t active_transaction_id_{0};
  uint8_t active_unit_id_{0};
};

}  // namespace modbus_tcp
}  // namespace sunspec
}  // namespace esphome
