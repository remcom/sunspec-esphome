#pragma once

#include "esphome/core/component.h"
#include "modbus.h"

#ifdef USE_ESP32
#include <lwip/sockets.h>
#else
#include <sys/socket.h>
#endif

#include <vector>
#include <map>

namespace esphome {
namespace sunspec {
namespace modbus_tcp {

struct ClientConnection {
  int socket{-1};
  bool active{false};
};

/// Modbus TCP server
class ModbusTCP : public Component {
 public:
  ModbusTCP();
  ~ModbusTCP() override;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  /// Set the TCP port to listen on
  void set_port(uint16_t port) { this->port_ = port; }

  /// Set the maximum number of concurrent connections
  void set_max_connections(uint8_t max_connections) { this->max_connections_ = max_connections; }

  /// Register a Modbus device
  void register_device(modbus::ModbusDevice *device);

  /// Send a response to a client
  void send_response(size_t conn_id, uint16_t transaction_id, uint8_t unit_id,
                     const std::vector<uint8_t> &data);

  /// Get active connection context (for use by devices during callbacks)
  size_t get_active_conn_id() const { return this->active_conn_id_; }
  uint16_t get_active_transaction_id() const { return this->active_transaction_id_; }
  uint8_t get_active_unit_id() const { return this->active_unit_id_; }

 protected:
  void accept_connection_();
  void handle_client_(size_t conn_id);
  void close_connection_(size_t conn_id);
  void process_modbus_request_(size_t conn_id, uint16_t transaction_id, uint8_t unit_id,
                               uint8_t function_code, const std::vector<uint8_t> &data);

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
