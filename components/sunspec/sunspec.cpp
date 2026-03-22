#include "sunspec.h"

#include <cstring>
#include <cmath>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>

#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace sunspec {

// ---------- helpers ----------

int16_t SunspecComponent::to_sf(float val, int sf) {
  if (std::isnan(val)) return (int16_t) 0x8000;
  float scaled = val * std::pow(10.0f, (float)(-sf));
  if (scaled > 32767.0f)  return 32767;
  if (scaled < -32768.0f) return -32768;
  return (int16_t)(scaled + (scaled >= 0 ? 0.5f : -0.5f));
}

void SunspecComponent::encode_string_(uint16_t *dest, const std::string &s, uint8_t reg_count) {
  // Pack ASCII bytes as big-endian uint16 pairs, null-padded
  uint8_t buf[reg_count * 2];
  memset(buf, 0, sizeof(buf));
  size_t copy_len = std::min(s.size(), (size_t)(reg_count * 2));
  memcpy(buf, s.c_str(), copy_len);
  for (uint8_t i = 0; i < reg_count; i++) {
    dest[i] = ((uint16_t)buf[i * 2] << 8) | buf[i * 2 + 1];
  }
}

// ---------- static register map ----------

void SunspecComponent::init_static_registers_() {
  // Initialise entire bank to 0xFFFF (SunSpec "not implemented")
  for (uint16_t i = 0; i < REG_COUNT; i++) registers_[i] = 0xFFFF;

  // --- Common Block (Model 1) ---
  set_reg(40000, 0x5375);  // SunS marker high
  set_reg(40001, 0x6e53);  // SunS marker low
  set_reg(40002, 1);       // Model ID
  set_reg(40003, 66);      // Length (data registers only)

  encode_string_(&registers_[4],  manufacturer_,  16);  // 40004-40019
  encode_string_(&registers_[20], model_,         16);  // 40020-40035
  encode_string_(&registers_[36], "",              8);  // 40036-40043 (options, empty)
  encode_string_(&registers_[44], version_,        8);  // 40044-40051
  encode_string_(&registers_[52], serial_number_, 16);  // 40052-40067

  set_reg(40068, 1);       // Modbus address
  set_reg(40069, 0xFFFF);  // Padding

  // --- Inverter Block (Model 101) ---
  set_reg(40070, 101);  // Model ID
  set_reg(40071, 50);   // Length
  // 40072-40075: current -- updated in refresh_sensors_()
  set_reg(40076, (uint16_t)(int16_t)(-2));  // Current SF = -2
  // 40077-40079: phase voltage AB/BC/CA -- stay 0xFFFF
  // 40080: Volts AN -- updated in refresh_sensors_()
  // 40081-40082: BN/CN -- stay 0xFFFF
  set_reg(40083, (uint16_t)(int16_t)(-1));  // Voltage SF = -1
  // 40084: AC power -- updated in refresh_sensors_()
  set_reg(40085, 0);  // Power SF = 0
  // 40086: Frequency -- updated in refresh_sensors_()
  set_reg(40087, (uint16_t)(int16_t)(-2));  // Frequency SF = -2
  // 40088-40093: VA/VAR/PF -- stay 0xFFFF
  // 40094-40095: energy -- updated in refresh_sensors_()
  set_reg(40096, 0);  // Energy SF = 0
  // 40097-40102: DC measurements -- stay 0xFFFF
  // 40103: temperature -- updated in refresh_sensors_()
  // 40104-40106: reserved -- stay 0xFFFF
  set_reg(40107, (uint16_t)(int16_t)(-1));  // Temp SF = -1
  set_reg(40108, 1);   // State = Off (updated in refresh_sensors_())
  // 40109: reserved -- stays 0xFFFF
  // 40110-40113: Events = 0 (no events)
  set_reg(40110, 0); set_reg(40111, 0); set_reg(40112, 0); set_reg(40113, 0);
  // 40114-40121: padding -- stays 0xFFFF

  // --- Nameplate Block (Model 120) ---
  set_reg(40122, 120);  // Model ID
  set_reg(40123, 26);   // Length
  set_reg(40124, 4);    // DER type = PV
  set_reg(40125, rated_power_);  // WRtg (watts, SF=0)
  set_reg(40126, 0);    // Power SF = 0
  // 40127-40149: stay 0xFFFF

  // --- Controls Block (Model 123) ---
  set_reg(40150, 123);  // Model ID
  set_reg(40151, 24);   // Length
  // 40152-40154: reserved -- stay 0xFFFF
  set_reg(40155, 100);  // WMaxLimPct default = 100 (no limit)
  // 40156: reserved -- stays 0xFFFF
  set_reg(40157, 0);    // WMaxLimPct_RvrtTms = 0 (no auto-revert)
  // 40158: reserved -- stays 0xFFFF
  set_reg(40159, 0);    // WMaxLim_Ena = 0 (disabled)
  // 40160-40172: reserved -- stay 0xFFFF
  set_reg(40173, 0);    // WMaxLimPct_SF = 0
  // 40174-40175: reserved -- stay 0xFFFF

  // --- End Marker ---
  set_reg(40176, 0xFFFF); set_reg(40177, 0xFFFF);
  set_reg(40178, 0xFFFF); set_reg(40179, 0xFFFF);
}

// ---------- lifecycle ----------

void SunspecComponent::setup() {
  // 1. Validate required sensor pointers
  if (!ac_power_ || !ac_voltage_ || !ac_current_ || !ac_frequency_ || !temperature_) {
    ESP_LOGE(TAG, "One or more required sensors are not configured");
    mark_failed();
    return;
  }

  // 2. Initialise register bank
  init_static_registers_();

  // 3. TCP socket -- implemented in Task 4
  server_fd_ = -1;
}

void SunspecComponent::loop() {
  if (server_fd_ < 0) return;

  // Implemented in later tasks
}

// ---------- stubs ----------

void SunspecComponent::refresh_sensors_() { /* Task 5 */ }
void SunspecComponent::process_client_(Client &c) { /* Task 6 */ }

}  // namespace sunspec
}  // namespace esphome
