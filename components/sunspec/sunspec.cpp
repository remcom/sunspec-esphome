#include "sunspec.h"

#include <cstring>
#include <cmath>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome::sunspec {

static const char *const TAG = "sunspec";

// ---------- helpers ----------

int16_t SunspecComponent::to_sf(float val, int sf) {
  if (std::isnan(val)) return (int16_t) 0x8000;
  float scaled = val * std::pow(10.0f, (float)(-sf));
  if (scaled > 32767.0f)  return 32767;
  if (scaled < -32768.0f) return -32768;
  return (int16_t)(scaled + (scaled >= 0 ? 0.5f : -0.5f));
}

void SunspecComponent::encode_string_(uint16_t *dest, const std::string &s, uint8_t reg_count) {
  // Pack ASCII bytes as big-endian uint16 pairs, null-padded.
  // reg_count is at most 16, so 32 bytes is a safe fixed upper bound.
  uint8_t buf[32];
  memset(buf, 0, reg_count * 2);
  size_t copy_len = std::min(s.size(), (size_t)(reg_count * 2));
  memcpy(buf, s.c_str(), copy_len);
  for (uint8_t i = 0; i < reg_count; i++) {
    dest[i] = ((uint16_t)buf[i * 2] << 8) | buf[i * 2 + 1];
  }
}

// ---------- static register map ----------

void SunspecComponent::init_static_registers_() {
  // Initialise entire bank to 0xFFFF (SunSpec "not implemented")
  for (uint16_t i = 0; i < REG_COUNT; i++) this->registers_[i] = 0xFFFF;

  // --- Common Block (Model 1) ---
  this->set_reg(40000, 0x5375);  // SunS marker high
  this->set_reg(40001, 0x6e53);  // SunS marker low
  this->set_reg(40002, 1);       // Model ID
  this->set_reg(40003, 66);      // Length (data registers only)

  this->encode_string_(&this->registers_[4],  this->manufacturer_,  16);  // 40004-40019
  this->encode_string_(&this->registers_[20], this->model_,         16);  // 40020-40035
  this->encode_string_(&this->registers_[36], "",                    8);  // 40036-40043 (options, empty)
  this->encode_string_(&this->registers_[44], this->version_,        8);  // 40044-40051
  this->encode_string_(&this->registers_[52], this->serial_number_, 16);  // 40052-40067

  this->set_reg(40068, 1);       // Modbus address
  this->set_reg(40069, 0xFFFF);  // Padding

  // --- Inverter Block (Model 101) ---
  this->set_reg(40070, 101);  // Model ID
  this->set_reg(40071, 50);   // Length
  // 40072-40075: current -- updated in refresh_sensors_()
  this->set_reg(40076, (uint16_t)(int16_t)(-2));  // Current SF = -2
  // 40077-40079: phase voltage AB/BC/CA -- stay 0xFFFF
  // 40080: Volts AN -- updated in refresh_sensors_()
  // 40081-40082: BN/CN -- stay 0xFFFF
  this->set_reg(40083, (uint16_t)(int16_t)(-1));  // Voltage SF = -1
  // 40084: AC power -- updated in refresh_sensors_()
  this->set_reg(40085, 0);  // Power SF = 0
  // 40086: Frequency -- updated in refresh_sensors_()
  this->set_reg(40087, (uint16_t)(int16_t)(-2));  // Frequency SF = -2
  // 40088-40093: VA/VAR/PF -- stay 0xFFFF
  // 40094-40095: energy -- updated in refresh_sensors_()
  this->set_reg(40096, 0);  // Energy SF = 0
  // 40097-40102: DC measurements -- stay 0xFFFF
  // 40103: temperature -- updated in refresh_sensors_()
  // 40104-40106: reserved -- stay 0xFFFF
  this->set_reg(40107, (uint16_t)(int16_t)(-1));  // Temp SF = -1
  this->set_reg(40108, 1);   // State = Off (updated in refresh_sensors_())
  // 40109: reserved -- stays 0xFFFF
  // 40110-40113: Events = 0 (no events)
  this->set_reg(40110, 0); this->set_reg(40111, 0);
  this->set_reg(40112, 0); this->set_reg(40113, 0);
  // 40114-40121: padding -- stays 0xFFFF

  // --- Nameplate Block (Model 120) ---
  this->set_reg(40122, 120);  // Model ID
  this->set_reg(40123, 26);   // Length
  this->set_reg(40124, 4);    // DER type = PV
  this->set_reg(40125, this->rated_power_);  // WRtg (watts, SF=0)
  this->set_reg(40126, 0);    // Power SF = 0
  // 40127-40149: stay 0xFFFF

  // --- Controls Block (Model 123) ---
  this->set_reg(40150, 123);  // Model ID
  this->set_reg(40151, 24);   // Length
  // 40152-40154: reserved -- stay 0xFFFF
  this->set_reg(40155, 100);  // WMaxLimPct default = 100 (no limit)
  // 40156: reserved -- stays 0xFFFF
  this->set_reg(40157, 0);    // WMaxLimPct_RvrtTms = 0 (no auto-revert)
  // 40158: reserved -- stays 0xFFFF
  this->set_reg(40159, 0);    // WMaxLim_Ena = 0 (disabled)
  // 40160-40172: reserved -- stay 0xFFFF
  this->set_reg(40173, 0);    // WMaxLimPct_SF = 0
  // 40174-40175: reserved -- stay 0xFFFF

  // --- End Marker ---
  this->set_reg(40176, 0xFFFF); this->set_reg(40177, 0xFFFF);
  this->set_reg(40178, 0xFFFF); this->set_reg(40179, 0xFFFF);
}

// ---------- lifecycle ----------

void SunspecComponent::setup() {
  // 1. Validate required sensor pointers
  if (!this->ac_power_ || !this->ac_voltage_ || !this->ac_frequency_ || !this->temperature_) {
    ESP_LOGE(TAG, "One or more required sensors are not configured");
    this->mark_failed();
    return;
  }

  // 2. Initialise register bank
  this->init_static_registers_();

  // 3. Open non-blocking TCP socket on port 502
  this->server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (this->server_fd_ < 0) {
    ESP_LOGE(TAG, "socket() failed: %d", errno);
    this->mark_failed();
    return;
  }

  int opt = 1;
  ::setsockopt(this->server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(502);

  if (::bind(this->server_fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "bind() failed on port 502: %d", errno);
    ::close(this->server_fd_);
    this->server_fd_ = -1;
    this->mark_failed();
    return;
  }

  if (::listen(this->server_fd_, 2) < 0) {
    ESP_LOGE(TAG, "listen() failed: %d", errno);
    ::close(this->server_fd_);
    this->server_fd_ = -1;
    this->mark_failed();
    return;
  }

  // Set non-blocking
  int flags = ::fcntl(this->server_fd_, F_GETFL, 0);
  if (flags < 0 || ::fcntl(this->server_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
    ESP_LOGE(TAG, "fcntl() failed: %d", errno);
    ::close(this->server_fd_);
    this->server_fd_ = -1;
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "SunSpec Modbus TCP server listening on port 502");
}

void SunspecComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "SunSpec Modbus TCP:");
  ESP_LOGCONFIG(TAG, "  Manufacturer: %s", this->manufacturer_.c_str());
  ESP_LOGCONFIG(TAG, "  Model:        %s", this->model_.c_str());
  ESP_LOGCONFIG(TAG, "  Serial:       %s", this->serial_number_.c_str());
  ESP_LOGCONFIG(TAG, "  Version:      %s", this->version_.c_str());
  ESP_LOGCONFIG(TAG, "  Rated Power:  %u W", this->rated_power_);
  ESP_LOGCONFIG(TAG, "  Port:         502");
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Setup failed!");
  }
}

void SunspecComponent::loop() {
  if (this->server_fd_ < 0) return;

  this->accept_clients_();
  this->refresh_sensors_();

  for (auto &c : this->clients_) {
    if (c.fd >= 0) this->process_client_(c);
  }
}

void SunspecComponent::accept_clients_() {
  struct sockaddr_in client_addr{};
  socklen_t addr_len = sizeof(client_addr);
  int fd = ::accept(this->server_fd_, (struct sockaddr *)&client_addr, &addr_len);
  if (fd < 0) return;  // EAGAIN / EWOULDBLOCK -- no pending connection

  // Find a free slot
  for (auto &c : this->clients_) {
    if (c.fd < 0) {
      int flags = ::fcntl(fd, F_GETFL, 0);
      if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ESP_LOGW(TAG, "fcntl() failed for client (fd=%d): %d", fd, errno);
        ::close(fd);
        return;
      }
      c.fd           = fd;
      c.buf_len      = 0;
      c.last_recv_ms = millis();
      ESP_LOGD(TAG, "Client connected (fd=%d)", fd);
      return;
    }
  }

  // No free slot -- reject
  ESP_LOGW(TAG, "Max clients reached, rejecting connection");
  ::close(fd);
}

void SunspecComponent::close_client_(Client &c) {
  if (c.fd >= 0) {
    ESP_LOGD(TAG, "Closing client (fd=%d)", c.fd);
    ::close(c.fd);
    c.fd      = -1;
    c.buf_len = 0;
  }
}

// ---------- sensor refresh ----------

void SunspecComponent::refresh_sensors_() {
  // AC current (SF=-2): value x 100, same on total and phase A.
  // If no current sensor, derive from power / voltage (assuming unity power factor).
  if (this->ac_current_) {
    int16_t curr = to_sf(this->ac_current_->get_state(), -2);
    this->set_reg(40072, (uint16_t) curr);  // AC total current
    this->set_reg(40073, (uint16_t) curr);  // Phase A current
  } else {
    float pwr  = this->ac_power_->get_state();
    float volt = this->ac_voltage_->get_state();
    if (!std::isnan(pwr) && !std::isnan(volt) && volt > 1.0f) {
      int16_t curr = to_sf(pwr / volt, -2);
      this->set_reg(40072, (uint16_t) curr);
      this->set_reg(40073, (uint16_t) curr);
    } else {
      this->set_reg(40072, 0x8000);
      this->set_reg(40073, 0x8000);
    }
  }

  // AC voltage (SF=-1): value x 10
  this->set_reg(40080, (uint16_t) to_sf(this->ac_voltage_->get_state(), -1));

  // AC power (SF=0): direct watts
  this->set_reg(40084, (uint16_t) to_sf(this->ac_power_->get_state(), 0));

  // AC frequency (SF=-2): value x 100
  this->set_reg(40086, (uint16_t) to_sf(this->ac_frequency_->get_state(), -2));

  // Energy (uint32, SF=0)
  if (this->energy_total_) {
    float e = this->energy_total_->get_state();
    if (!std::isnan(e) && e >= 0) {
      uint32_t wh = (uint32_t)(e * 1000.0f);
      this->set_reg(40094, (uint16_t)(wh >> 16));
      this->set_reg(40095, (uint16_t)(wh & 0xFFFF));
    } else {
      this->set_reg(40094, 0);
      this->set_reg(40095, 0);
    }
  }

  // Temperature (SF=-1): value x 10
  this->set_reg(40103, (uint16_t) to_sf(this->temperature_->get_state(), -1));

  // Inverter state: MPPT (4) if producing, else Off (1)
  float pwr = this->ac_power_->get_state();
  this->set_reg(40108, (!std::isnan(pwr) && pwr > 5.0f) ? 4 : 1);
}

// ---------- TCP framing ----------

void SunspecComponent::process_client_(Client &c) {
  // Timeout check
  if (millis() - c.last_recv_ms > CLIENT_TIMEOUT_MS) {
    ESP_LOGD(TAG, "Client timeout (fd=%d)", c.fd);
    this->close_client_(c);
    return;
  }

  // Read available bytes
  int n = ::recv(c.fd, c.buf + c.buf_len, MAX_BUF - c.buf_len, 0);
  if (n == 0) { this->close_client_(c); return; }
  if (n < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) this->close_client_(c);
    return;
  }
  c.buf_len     += n;
  c.last_recv_ms = millis();

  // Overflow guard
  if (c.buf_len >= MAX_BUF) {
    ESP_LOGD(TAG, "Client buffer overflow (fd=%d), closing", c.fd);
    this->close_client_(c);
    return;
  }

  // Process all complete frames in buffer
  while (c.buf_len >= 6) {
    uint16_t proto_id   = (c.buf[2] << 8) | c.buf[3];
    uint16_t pdu_length = (c.buf[4] << 8) | c.buf[5];  // bytes after 6-byte MBAP header

    // Validate protocol ID
    if (proto_id != 0x0000) {
      ESP_LOGD(TAG, "Invalid protocol ID %04X, closing", proto_id);
      this->close_client_(c);
      return;
    }

    uint16_t frame_len = 6 + pdu_length;  // total frame bytes
    if (frame_len > MAX_BUF) {
      ESP_LOGD(TAG, "Frame too large (%u bytes), closing", frame_len);
      this->close_client_(c);
      return;
    }

    if (c.buf_len < frame_len) break;  // incomplete frame, wait for more data

    this->handle_frame_(c, frame_len);

    // Consume processed frame from buffer
    c.buf_len -= frame_len;
    if (c.buf_len > 0) memmove(c.buf, c.buf + frame_len, c.buf_len);
  }
}

void SunspecComponent::handle_frame_(Client &c, uint16_t frame_len) {
  uint16_t txid = (c.buf[0] << 8) | c.buf[1];
  uint8_t  uid  = c.buf[6];
  uint8_t  fc   = c.buf[7];

  if (fc == 0x03) {
    // FC03: Read Holding Registers
    if (frame_len < 12) { this->send_exception_(c.fd, txid, uid, fc, 0x03); return; }
    uint16_t start = (c.buf[8]  << 8) | c.buf[9];
    uint16_t count = (c.buf[10] << 8) | c.buf[11];

    if (count > 120) { this->send_exception_(c.fd, txid, uid, fc, 0x03); return; }
    if (start < BASE_ADDR || (start + count) > (BASE_ADDR + REG_COUNT)) {
      this->send_exception_(c.fd, txid, uid, fc, 0x02);
      return;
    }

    // Max count is 120 registers → 2 + 240 = 242 bytes
    uint8_t pdu[242];
    pdu[0] = fc;
    pdu[1] = count * 2;
    uint16_t idx = start - BASE_ADDR;
    for (uint16_t i = 0; i < count; i++) {
      pdu[2 + i * 2]     = this->registers_[idx + i] >> 8;
      pdu[2 + i * 2 + 1] = this->registers_[idx + i] & 0xFF;
    }
    this->send_response_(c.fd, txid, uid, pdu, 2 + count * 2);

  } else if (fc == 0x06) {
    // FC06: Write Single Register
    if (frame_len < 12) { this->send_exception_(c.fd, txid, uid, fc, 0x03); return; }
    uint16_t addr = (c.buf[8]  << 8) | c.buf[9];
    uint16_t val  = (c.buf[10] << 8) | c.buf[11];

    if (addr != REG_WMAXLIMPCT && addr != REG_WMAXLIM_ENA) {
      this->send_exception_(c.fd, txid, uid, fc, 0x02);
      return;
    }
    this->set_reg(addr, val);
    this->apply_power_limit_();

    // Echo back per Modbus spec
    uint8_t pdu[5] = { fc, c.buf[8], c.buf[9], c.buf[10], c.buf[11] };
    this->send_response_(c.fd, txid, uid, pdu, sizeof(pdu));

  } else if (fc == 0x10) {
    // FC16: Write Multiple Registers
    if (frame_len < 13) { this->send_exception_(c.fd, txid, uid, fc, 0x03); return; }
    uint16_t start = (c.buf[8]  << 8) | c.buf[9];
    uint16_t count = (c.buf[10] << 8) | c.buf[11];

    if (count > 120 || frame_len < (uint16_t)(13 + count * 2)) {
      this->send_exception_(c.fd, txid, uid, fc, 0x03);
      return;
    }

    // Apply only writable registers; silently ignore others
    bool changed = false;
    for (uint16_t i = 0; i < count; i++) {
      uint16_t addr = start + i;
      uint16_t val  = (c.buf[13 + i * 2] << 8) | c.buf[14 + i * 2];
      if (addr == REG_WMAXLIMPCT || addr == REG_WMAXLIM_ENA) {
        this->set_reg(addr, val);
        changed = true;
      }
    }
    if (changed) this->apply_power_limit_();

    // Always respond with success — non-writable addresses are silently ignored per spec.
    uint8_t pdu[5] = { fc, c.buf[8], c.buf[9], c.buf[10], c.buf[11] };
    this->send_response_(c.fd, txid, uid, pdu, sizeof(pdu));

  } else {
    this->send_exception_(c.fd, txid, uid, fc, 0x01);  // Illegal Function
  }
}

void SunspecComponent::send_response_(int fd, uint16_t txid, uint8_t uid,
                                       const uint8_t *pdu, uint8_t pdu_len) {
  // Max frame: 7-byte MBAP header + up to 242-byte PDU = 249 bytes
  uint8_t frame[249];
  frame[0] = txid >> 8;   frame[1] = txid & 0xFF;
  frame[2] = 0x00;        frame[3] = 0x00;  // Protocol ID
  frame[4] = (1 + pdu_len) >> 8;
  frame[5] = (1 + pdu_len) & 0xFF;
  frame[6] = uid;
  memcpy(frame + 7, pdu, pdu_len);
  uint16_t frame_len = 7 + pdu_len;
  if (::send(fd, frame, frame_len, 0) < 0) {
    ESP_LOGW(TAG, "send() failed (fd=%d): %d", fd, errno);
  }
}

void SunspecComponent::send_exception_(int fd, uint16_t txid, uint8_t uid,
                                        uint8_t fc, uint8_t code) {
  uint8_t pdu[2] = { (uint8_t)(fc | 0x80), code };
  this->send_response_(fd, txid, uid, pdu, 2);
}

void SunspecComponent::apply_power_limit_() {
  uint16_t ena = this->get_reg(REG_WMAXLIM_ENA);
  uint16_t pct = this->get_reg(REG_WMAXLIMPCT);

  if (this->power_limit_number_) {
    // Route through the number entity: 0–100 = limit %, 110 = unlimited
    float set_val = (ena == 1) ? (float) pct : 110.0f;
    auto call = this->power_limit_number_->make_call();
    call.set_value(set_val);
    call.perform();
    if (ena == 1) {
      ESP_LOGI(TAG, "Power limit set to %u%% via number entity", pct);
    } else {
      ESP_LOGI(TAG, "Power limit disabled (110 = unlimited) via number entity");
    }
    return;
  }

  // Fallback: write directly to Modbus register
  uint16_t write_val = (ena == 1) ? pct : 100;
  if (!this->controller_) {
    ESP_LOGW(TAG, "No ModbusController configured, cannot write power limit");
    return;
  }
  auto cmd = modbus_controller::ModbusCommandItem::create_write_single_command(
      this->controller_, this->power_limit_register_, write_val);
  this->controller_->queue_command(cmd);
  if (ena == 1) {
    ESP_LOGI(TAG, "Power limit set to %u%%", pct);
  } else {
    ESP_LOGI(TAG, "Power limit disabled, restoring 100%%");
  }
}

}  // namespace esphome::sunspec
