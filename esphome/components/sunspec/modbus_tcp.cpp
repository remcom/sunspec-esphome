#include "modbus_tcp.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <cerrno>
#include <cstring>

namespace esphome {
namespace sunspec {

static const char *const TAG = "modbus_tcp";

namespace modbus {

void ModbusDevice::send_error(uint8_t function_code, ModbusExceptionCode exception_code) {
  std::vector<uint8_t> response;
  response.push_back(function_code | 0x80);  // Error function code
  response.push_back(static_cast<uint8_t>(exception_code));

  if (this->active_tcp_server_ != nullptr) {
    this->active_tcp_server_->send_response(this->active_conn_id_, this->active_transaction_id_,
                                            this->active_unit_id_, response);
  }
}

}  // namespace modbus

namespace modbus_tcp {

// Modbus TCP servers must accept requests addressed to unit ID 0xFF (the
// recommended default for devices without a unit hierarchy, per the Modbus
// Messaging on TCP/IP Implementation Guide)
static constexpr uint8_t UNIT_ID_DEFAULT = 0xFF;

ModbusTCP::~ModbusTCP() {
  for (size_t i = 0; i < this->connections_.size(); i++) {
    close_connection_(i);
  }
  if (this->server_socket_ >= 0) {
    close(this->server_socket_);
    this->server_socket_ = -1;
  }
}

bool ModbusTCP::setup() {
  this->connections_.resize(this->max_connections_);

  this->server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (this->server_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
    return false;
  }

  int opt = 1;
  if (setsockopt(this->server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    ESP_LOGW(TAG, "Failed to set SO_REUSEADDR: errno %d", errno);
  }

  // Set non-blocking
  int flags = fcntl(this->server_socket_, F_GETFL, 0);
  fcntl(this->server_socket_, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(this->port_);

  if (bind(this->server_socket_, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind socket: errno %d", errno);
    return false;
  }

  if (listen(this->server_socket_, this->max_connections_) < 0) {
    ESP_LOGE(TAG, "Failed to listen: errno %d", errno);
    return false;
  }

  return true;
}

void ModbusTCP::loop() {
  if (this->server_socket_ < 0)
    return;

  // Accept new connections
  accept_connection_();

  // Handle existing connections; reap ones that have been silent too long
  uint32_t now = millis();
  for (size_t i = 0; i < this->connections_.size(); i++) {
    if (!this->connections_[i].active)
      continue;
    if (now - this->connections_[i].last_activity > IDLE_TIMEOUT_MS) {
      ESP_LOGI(TAG, "Closing idle client %u", i);
      close_connection_(i);
      continue;
    }
    handle_client_(i);
  }
}

uint8_t ModbusTCP::get_client_count() const {
  uint8_t count = 0;
  for (const auto &conn : this->connections_) {
    if (conn.active)
      count++;
  }
  return count;
}

void ModbusTCP::register_device(modbus::ModbusDevice *device) {
  this->devices_.push_back(device);
  ESP_LOGD(TAG, "Registered device at unit address 0x%02X", device->address_);
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

  // Send, handling partial writes on the non-blocking socket
  int sock = this->connections_[conn_id].socket;
  size_t offset = 0;
  int retries = 0;
  while (offset < response.size()) {
    ssize_t sent = send(sock, response.data() + offset, response.size() - offset, 0);
    if (sent > 0) {
      offset += sent;
      continue;
    }
    if (sent < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
      if (++retries > 50) {
        ESP_LOGW(TAG, "Send timed out on client %u", conn_id);
        close_connection_(conn_id);
        return;
      }
      delay(1);
      continue;
    }
    ESP_LOGW(TAG, "Failed to send response: errno %d", errno);
    close_connection_(conn_id);
    return;
  }
  ESP_LOGD(TAG, "Sent %u bytes to client %u", response.size(), conn_id);
}

void ModbusTCP::send_exception_(size_t conn_id, uint16_t transaction_id, uint8_t unit_id, uint8_t function_code,
                                modbus::ModbusExceptionCode exception_code) {
  std::vector<uint8_t> response;
  response.push_back(function_code | 0x80);
  response.push_back(static_cast<uint8_t>(exception_code));
  this->send_response(conn_id, transaction_id, unit_id, response);
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

  if (conn_id >= this->connections_.size()) {
    ESP_LOGW(TAG, "Max connections reached, rejecting new connection");
    close(client_socket);
    return;
  }

  // Set non-blocking
  int flags = fcntl(client_socket, F_GETFL, 0);
  fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);

  // Small responses to poll requests should not wait for Nagle
  int opt = 1;
  setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
  setsockopt(client_socket, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

  // Store connection
  this->connections_[conn_id].socket = client_socket;
  this->connections_[conn_id].active = true;
  this->connections_[conn_id].last_activity = millis();

  ESP_LOGI(TAG, "New client connected (slot %u) from %s", conn_id, inet_ntoa(client_addr.sin_addr));
}

void ModbusTCP::handle_client_(size_t conn_id) {
  int sock = this->connections_[conn_id].socket;

  // Read MBAP header (7 bytes)
  uint8_t header[7];
  ssize_t header_len = recv(sock, header, sizeof(header), MSG_PEEK);

  if (header_len == 0) {
    // Connection closed
    ESP_LOGD(TAG, "Client %u disconnected", conn_id);
    close_connection_(conn_id);
    return;
  }

  if (header_len < 0) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      ESP_LOGW(TAG, "Recv error on client %u: errno %d", conn_id, errno);
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
    ESP_LOGW(TAG, "Request too large: %u bytes", total_len);
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
  this->connections_[conn_id].last_activity = millis();

  // Extract PDU (skip MBAP header)
  if (length < 2) {
    ESP_LOGW(TAG, "PDU too short");
    return;
  }

  uint8_t function_code = buffer[7];
  std::vector<uint8_t> pdu_data(buffer + 8, buffer + total_len);

  ESP_LOGD(TAG, "Request: TID=%d, Unit=%d, FC=0x%02X, Len=%u", transaction_id, unit_id, function_code,
           pdu_data.size());

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
  // Find the device with matching address; 0xFF addresses the (single) device directly
  modbus::ModbusDevice *device = nullptr;
  for (auto *dev : this->devices_) {
    if (dev->address_ == unit_id || (unit_id == UNIT_ID_DEFAULT && !this->devices_.empty())) {
      device = dev;
      break;
    }
  }

  if (device == nullptr) {
    ESP_LOGW(TAG, "No device found for unit ID 0x%02X", unit_id);
    this->send_exception_(conn_id, transaction_id, unit_id, function_code,
                          modbus::ModbusExceptionCode::GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND);
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
      this->send_exception_(conn_id, transaction_id, unit_id, function_code,
                            modbus::ModbusExceptionCode::ILLEGAL_DATA_VALUE);
      return;
    }

    uint16_t start_address = (data[0] << 8) | data[1];
    uint16_t num_registers = (data[2] << 8) | data[3];

    device->on_modbus_read_registers(function_code, start_address, num_registers);
  } else if (function_code == 0x06) {
    // Write Single Register
    if (data.size() < 4) {
      this->send_exception_(conn_id, transaction_id, unit_id, function_code,
                            modbus::ModbusExceptionCode::ILLEGAL_DATA_VALUE);
      return;
    }

    uint16_t address = (data[0] << 8) | data[1];
    std::vector<uint16_t> values{static_cast<uint16_t>((data[2] << 8) | data[3])};
    device->on_modbus_write_registers(function_code, address, values);
  } else if (function_code == 0x10) {
    // Write Multiple Registers
    if (data.size() < 5) {
      this->send_exception_(conn_id, transaction_id, unit_id, function_code,
                            modbus::ModbusExceptionCode::ILLEGAL_DATA_VALUE);
      return;
    }

    uint16_t start_address = (data[0] << 8) | data[1];
    uint16_t num_registers = (data[2] << 8) | data[3];
    uint8_t byte_count = data[4];
    if (num_registers == 0 || num_registers > 123 || byte_count != num_registers * 2 ||
        data.size() < static_cast<size_t>(5 + byte_count)) {
      this->send_exception_(conn_id, transaction_id, unit_id, function_code,
                            modbus::ModbusExceptionCode::ILLEGAL_DATA_VALUE);
      return;
    }

    std::vector<uint16_t> values;
    values.reserve(num_registers);
    for (uint16_t i = 0; i < num_registers; i++) {
      values.push_back((data[5 + i * 2] << 8) | data[6 + i * 2]);
    }
    device->on_modbus_write_registers(function_code, start_address, values);
  } else {
    // Unsupported function code
    this->send_exception_(conn_id, transaction_id, unit_id, function_code,
                          modbus::ModbusExceptionCode::ILLEGAL_FUNCTION);
  }
}

}  // namespace modbus_tcp
}  // namespace sunspec
}  // namespace esphome
