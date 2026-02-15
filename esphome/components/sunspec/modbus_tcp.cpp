#include "modbus_tcp.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace esphome {
namespace sunspec {

static const char *const TAG = "modbus_tcp";

namespace modbus {

void ModbusDevice::send_error(uint8_t function_code, ModbusExceptionCode exception_code) {
  std::vector<uint8_t> response;
  response.push_back(function_code | 0x80);  // Error function code
  response.push_back(static_cast<uint8_t>(exception_code));

  if (this->active_tcp_server_) {
    auto *tcp = static_cast<modbus_tcp::ModbusTCP *>(this->active_tcp_server_);
    tcp->send_response(this->active_conn_id_, this->active_transaction_id_, this->active_unit_id_, response);
  }
}

}  // namespace modbus

namespace modbus_tcp {

ModbusTCP::ModbusTCP() { this->connections_.resize(8); }

ModbusTCP::~ModbusTCP() {
  // Close all connections
  for (size_t i = 0; i < this->connections_.size(); i++) {
    close_connection_(i);
  }
  // Close server socket
  if (this->server_socket_ >= 0) {
    close(this->server_socket_);
    this->server_socket_ = -1;
  }
}

void ModbusTCP::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Modbus TCP server on port %d...", this->port_);

  // Create server socket
  this->server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (this->server_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
    this->mark_failed();
    return;
  }

  // Set socket options
  int opt = 1;
  if (setsockopt(this->server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    ESP_LOGW(TAG, "Failed to set SO_REUSEADDR: errno %d", errno);
  }

  // Set non-blocking
  int flags = fcntl(this->server_socket_, F_GETFL, 0);
  fcntl(this->server_socket_, F_SETFL, flags | O_NONBLOCK);

  // Bind to port
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(this->port_);

  if (bind(this->server_socket_, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind socket: errno %d", errno);
    this->mark_failed();
    return;
  }

  // Listen for connections
  if (listen(this->server_socket_, this->max_connections_) < 0) {
    ESP_LOGE(TAG, "Failed to listen: errno %d", errno);
    this->mark_failed();
    return;
  }

  ESP_LOGCONFIG(TAG, "Modbus TCP server listening on port %d", this->port_);
}

void ModbusTCP::loop() {
  if (this->server_socket_ < 0)
    return;

  // Accept new connections
  accept_connection_();

  // Handle existing connections
  for (size_t i = 0; i < this->connections_.size(); i++) {
    if (this->connections_[i].active) {
      handle_client_(i);
    }
  }
}

void ModbusTCP::dump_config() {
  ESP_LOGCONFIG(TAG, "Modbus TCP:");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
  ESP_LOGCONFIG(TAG, "  Max Connections: %d", this->max_connections_);
  ESP_LOGCONFIG(TAG, "  Registered Devices: %d", this->devices_.size());
}

void ModbusTCP::register_device(modbus::ModbusDevice *device) {
  this->devices_.push_back(device);
  ESP_LOGD(TAG, "Registered device at address 0x%02X", device->address_);
}

void ModbusTCP::send_response(size_t conn_id, uint16_t transaction_id, uint8_t unit_id,
                               const std::vector<uint8_t> &data) {
  if (conn_id >= this->connections_.size() || !this->connections_[conn_id].active) {
    return;
  }

  // Build MBAP header + PDU
  std::vector<uint8_t> response;
  response.reserve(7 + data.size());

  // Transaction ID (2 bytes)
  response.push_back(transaction_id >> 8);
  response.push_back(transaction_id & 0xFF);

  // Protocol ID (2 bytes) - always 0 for Modbus
  response.push_back(0);
  response.push_back(0);

  // Length (2 bytes) - unit ID + PDU length
  uint16_t length = 1 + data.size();
  response.push_back(length >> 8);
  response.push_back(length & 0xFF);

  // Unit ID (1 byte)
  response.push_back(unit_id);

  // PDU data
  response.insert(response.end(), data.begin(), data.end());

  // Send response
  int sock = this->connections_[conn_id].socket;
  ssize_t sent = send(sock, response.data(), response.size(), 0);
  if (sent < 0) {
    ESP_LOGW(TAG, "Failed to send response: errno %d", errno);
    close_connection_(conn_id);
  } else {
    ESP_LOGD(TAG, "Sent %d bytes to client %d", sent, conn_id);
  }
}

void ModbusTCP::accept_connection_() {
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);

  int client_socket = accept(this->server_socket_, (struct sockaddr *) &client_addr, &client_len);
  if (client_socket < 0) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      ESP_LOGW(TAG, "Accept failed: errno %d", errno);
    }
    return;
  }

  // Find a free connection slot
  size_t conn_id = 0;
  for (; conn_id < this->connections_.size(); conn_id++) {
    if (!this->connections_[conn_id].active) {
      break;
    }
  }

  if (conn_id >= this->max_connections_) {
    ESP_LOGW(TAG, "Max connections reached, rejecting new connection");
    close(client_socket);
    return;
  }

  // Set non-blocking
  int flags = fcntl(client_socket, F_GETFL, 0);
  fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);

  // Store connection
  this->connections_[conn_id].socket = client_socket;
  this->connections_[conn_id].active = true;

  ESP_LOGI(TAG, "New client connected (slot %d) from %s", conn_id, inet_ntoa(client_addr.sin_addr));
}

void ModbusTCP::handle_client_(size_t conn_id) {
  int sock = this->connections_[conn_id].socket;

  // Read MBAP header (7 bytes)
  uint8_t header[7];
  ssize_t header_len = recv(sock, header, sizeof(header), MSG_PEEK);

  if (header_len == 0) {
    // Connection closed
    ESP_LOGD(TAG, "Client %d disconnected", conn_id);
    close_connection_(conn_id);
    return;
  }

  if (header_len < 0) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      ESP_LOGW(TAG, "Recv error on client %d: errno %d", conn_id, errno);
      close_connection_(conn_id);
    }
    return;
  }

  if (header_len < 7) {
    // Not enough data yet
    return;
  }

  // Parse MBAP header
  uint16_t transaction_id = (header[0] << 8) | header[1];
  uint16_t protocol_id = (header[2] << 8) | header[3];
  uint16_t length = (header[4] << 8) | header[5];
  uint8_t unit_id = header[6];

  if (protocol_id != 0) {
    ESP_LOGW(TAG, "Invalid protocol ID: %d", protocol_id);
    close_connection_(conn_id);
    return;
  }

  // Check if we have the full PDU
  size_t total_len = 7 + length - 1;  // MBAP header + PDU (length includes unit ID)
  uint8_t buffer[256];
  if (total_len > sizeof(buffer)) {
    ESP_LOGW(TAG, "Request too large: %d bytes", total_len);
    close_connection_(conn_id);
    return;
  }

  ssize_t received = recv(sock, buffer, total_len, MSG_PEEK);
  if (received < (ssize_t) total_len) {
    // Not enough data yet
    return;
  }

  // Consume the data
  recv(sock, buffer, total_len, 0);

  // Extract PDU (skip MBAP header)
  if (length < 2) {
    ESP_LOGW(TAG, "PDU too short");
    return;
  }

  uint8_t function_code = buffer[7];
  std::vector<uint8_t> pdu_data(buffer + 8, buffer + total_len);

  ESP_LOGD(TAG, "Request: TID=%d, Unit=%d, FC=0x%02X, Len=%d", transaction_id, unit_id, function_code, pdu_data.size());

  // Process the request
  process_modbus_request_(conn_id, transaction_id, unit_id, function_code, pdu_data);
}

void ModbusTCP::close_connection_(size_t conn_id) {
  if (conn_id >= this->connections_.size())
    return;

  if (this->connections_[conn_id].socket >= 0) {
    close(this->connections_[conn_id].socket);
    this->connections_[conn_id].socket = -1;
  }
  this->connections_[conn_id].active = false;
}

void ModbusTCP::process_modbus_request_(size_t conn_id, uint16_t transaction_id, uint8_t unit_id,
                                        uint8_t function_code, const std::vector<uint8_t> &data) {
  // Find the device with matching address
  modbus::ModbusDevice *device = nullptr;
  for (auto *dev : this->devices_) {
    if (dev->address_ == unit_id) {
      device = dev;
      break;
    }
  }

  if (!device) {
    ESP_LOGW(TAG, "No device found for unit ID 0x%02X", unit_id);
    return;
  }

  // Set active context
  this->active_conn_id_ = conn_id;
  this->active_transaction_id_ = transaction_id;
  this->active_unit_id_ = unit_id;
  device->set_active_context(this, conn_id, transaction_id, unit_id);

  // Dispatch based on function code
  if (function_code == 0x03 || function_code == 0x04) {
    // Read Holding Registers (0x03) or Read Input Registers (0x04)
    if (data.size() < 4) {
      ESP_LOGW(TAG, "Invalid read registers request");
      return;
    }

    uint16_t start_address = (data[0] << 8) | data[1];
    uint16_t num_registers = (data[2] << 8) | data[3];

    device->on_modbus_read_registers(function_code, start_address, num_registers);
  } else {
    // Other function codes - pass raw data
    device->on_modbus_data(data);
  }
}

}  // namespace modbus_tcp
}  // namespace sunspec
}  // namespace esphome
