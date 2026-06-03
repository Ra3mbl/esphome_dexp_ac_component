#pragma once

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace chiq_ac {

static const char *const TAG = "chiq_ac";

class Constants {
 public:
  static constexpr const char *TURBO = "turbo";
  static constexpr const char *QUIET = "quiet";

  static constexpr float MIN_TEMPERATURE = 16.0f;
  static constexpr float MAX_TEMPERATURE = 32.0f;
  static constexpr float TEMPERATURE_STEP = 1.0f;
};

class ChiqAirCon : public climate::Climate, public Component, public uart::UARTDevice {
 public:
  void set_period(uint32_t period) { this->period_ = period; }
  void set_optimistic(bool optimistic) { this->optimistic_ = optimistic; }
  void set_current_temperature_sensor(sensor::Sensor *sensor) { this->current_temperature_sensor_ = sensor; }
  void set_turbo_sensor(binary_sensor::BinarySensor *sensor) { this->turbo_sensor_ = sensor; }
  void set_ionizer_sensor(binary_sensor::BinarySensor *sensor) { this->ionizer_sensor_ = sensor; }
  void set_quiet_sensor(binary_sensor::BinarySensor *sensor) { this->quiet_sensor_ = sensor; }
  void set_supported_modes(climate::ClimateModeMask modes) { this->supported_modes_ = modes; }
  void set_supported_swing_modes(climate::ClimateSwingModeMask modes) { this->supported_swing_modes_ = modes; }
  void set_supported_presets(climate::ClimatePresetMask presets) { this->supported_presets_ = presets; }
  void set_custom_fan_modes(std::initializer_list<const char *> modes) {
    this->supported_custom_fan_modes_.assign(modes.begin(), modes.end());
    this->set_supported_custom_fan_modes(this->supported_custom_fan_modes_);
  }

  void setup() override {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->target_temperature = 24.0f;
    this->current_temperature = NAN;
    this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
    this->swing_mode = climate::CLIMATE_SWING_OFF;
    this->preset = climate::CLIMATE_PRESET_NONE;
    this->set_supported_custom_fan_modes(this->supported_custom_fan_modes_);
  }

  void loop() override {
    this->read_uart_();

    const uint32_t now = millis();
    if (now - this->last_poll_ >= this->period_) {
      this->last_poll_ = now;
      this->send_poll_();
    }
  }

  climate::ClimateTraits traits() override {
    climate::ClimateTraits traits;
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
    traits.set_supported_modes(this->supported_modes_);
    traits.add_supported_mode(climate::CLIMATE_MODE_OFF);
    traits.add_supported_mode(climate::CLIMATE_MODE_HEAT_COOL);
    traits.add_supported_mode(climate::CLIMATE_MODE_COOL);
    traits.add_supported_mode(climate::CLIMATE_MODE_DRY);
    traits.add_supported_mode(climate::CLIMATE_MODE_HEAT);
    traits.add_supported_mode(climate::CLIMATE_MODE_FAN_ONLY);
    traits.add_supported_fan_mode(climate::CLIMATE_FAN_AUTO);
    traits.add_supported_fan_mode(climate::CLIMATE_FAN_LOW);
    traits.add_supported_fan_mode(climate::CLIMATE_FAN_MEDIUM);
    traits.add_supported_fan_mode(climate::CLIMATE_FAN_HIGH);
    traits.set_supported_swing_modes(this->supported_swing_modes_);
    traits.add_supported_swing_mode(climate::CLIMATE_SWING_OFF);
    traits.add_supported_swing_mode(climate::CLIMATE_SWING_HORIZONTAL);
    traits.set_supported_presets(this->supported_presets_);
    traits.add_supported_preset(climate::CLIMATE_PRESET_NONE);
    traits.add_supported_preset(climate::CLIMATE_PRESET_SLEEP);
    traits.set_visual_min_temperature(Constants::MIN_TEMPERATURE);
    traits.set_visual_max_temperature(Constants::MAX_TEMPERATURE);
    traits.set_visual_temperature_step(Constants::TEMPERATURE_STEP);
    return traits;
  }

  void control(const climate::ClimateCall &call) override {
    if (call.get_mode().has_value())
      this->mode = *call.get_mode();
    if (call.get_target_temperature().has_value())
      this->target_temperature = *call.get_target_temperature();
    if (call.get_fan_mode().has_value())
      this->set_fan_mode_(*call.get_fan_mode());
    if (call.get_swing_mode().has_value())
      this->swing_mode = *call.get_swing_mode();
    if (call.get_preset().has_value())
      this->preset = *call.get_preset();
    if (call.has_custom_fan_mode())
      this->set_custom_fan_mode_(call.get_custom_fan_mode());

    this->clamp_frontend_state_();
    this->send_command_();

    if (this->optimistic_)
      this->publish_state();
  }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "CHiQ/DEXP UART AC");
    ESP_LOGCONFIG(TAG, "  Poll period: %" PRIu32 " ms", this->period_);
    ESP_LOGCONFIG(TAG, "  Optimistic: %s", YESNO(this->optimistic_));
  }

 protected:
  static constexpr uint8_t HEADER = 0xAA;
  static constexpr uint8_t STATUS_LEN = 0x14;
  static constexpr uint8_t PAYLOAD_SIZE = 19;
  static constexpr uint8_t FRAME_BUFFER_SIZE = 24;

  enum RxState : uint8_t {
    RX_WAIT_HEADER,
    RX_WAIT_LEN,
    RX_WAIT_DATA,
  };

  void read_uart_() {
    while (this->available()) {
      const uint8_t byte = this->read();
      switch (this->rx_state_) {
        case RX_WAIT_HEADER:
          if (byte == HEADER) {
            this->rx_buffer_[0] = byte;
            this->rx_pos_ = 1;
            this->rx_state_ = RX_WAIT_LEN;
          }
          break;
        case RX_WAIT_LEN:
          if (byte == 0 || byte + 2 > FRAME_BUFFER_SIZE) {
            this->rx_state_ = RX_WAIT_HEADER;
            this->rx_pos_ = 0;
            break;
          }
          this->rx_buffer_[1] = byte;
          this->rx_expected_ = byte + 2;
          this->rx_pos_ = 2;
          this->rx_state_ = RX_WAIT_DATA;
          break;
        case RX_WAIT_DATA:
          this->rx_buffer_[this->rx_pos_++] = byte;
          if (this->rx_pos_ >= this->rx_expected_) {
            this->handle_frame_(this->rx_buffer_.data(), this->rx_expected_);
            this->rx_state_ = RX_WAIT_HEADER;
            this->rx_pos_ = 0;
          }
          break;
      }
    }
  }

  void handle_frame_(const uint8_t *frame, uint8_t size) {
    if (size < 4 || frame[0] != HEADER)
      return;

    const uint8_t checksum = this->checksum_(frame, size - 1);
    if (checksum != frame[size - 1]) {
      ESP_LOGW(TAG, "Bad checksum: got %02X, expected %02X", frame[size - 1], checksum);
      return;
    }

    ESP_LOGV(TAG, "RX frame len=%u kind=%02X", frame[1], frame[2]);

    if (frame[1] == STATUS_LEN)
      this->parse_status_payload_(&frame[2]);
  }

  void parse_status_payload_(const uint8_t *payload) {
    if (payload[0] != 0x01 && payload[0] != 0x02)
      return;

    std::copy(payload, payload + PAYLOAD_SIZE, this->last_payload_.begin());
    this->have_payload_ = true;

    this->mode = this->decode_mode_(payload[1], payload[5]);
    this->target_temperature = this->decode_temperature_(payload[8]);

    if (payload[9] != 0x00) {
      this->current_temperature = this->decode_temperature_(payload[9]);
      if (this->current_temperature_sensor_ != nullptr)
        this->current_temperature_sensor_->publish_state(this->current_temperature);
    }

    this->set_fan_mode_(this->decode_fan_(payload[7]));
    this->swing_mode = payload[4] == 0x1A ? climate::CLIMATE_SWING_HORIZONTAL : climate::CLIMATE_SWING_OFF;
    this->preset = (payload[6] & 0x01) ? climate::CLIMATE_PRESET_SLEEP : climate::CLIMATE_PRESET_NONE;
    this->clear_custom_fan_mode_();
    if (payload[6] & 0x08)
      this->set_custom_fan_mode_(Constants::TURBO);
    else if (payload[6] & 0x04)
      this->set_custom_fan_mode_(Constants::QUIET);

    if (this->turbo_sensor_ != nullptr)
      this->turbo_sensor_->publish_state(payload[6] & 0x08);
    if (this->ionizer_sensor_ != nullptr)
      this->ionizer_sensor_->publish_state(payload[6] & 0x02);
    if (this->quiet_sensor_ != nullptr)
      this->quiet_sensor_->publish_state(payload[6] & 0x04);

    this->publish_state();
  }

  void send_poll_() {
    const uint8_t frame[] = {0xAA, 0x02, 0x01, 0xAD};
    this->write_array(frame, sizeof(frame));
    this->flush();
    ESP_LOGV(TAG, "TX poll");
  }

  void send_command_() {
    std::array<uint8_t, PAYLOAD_SIZE> payload{};
    payload[0] = 0x02;
    payload[1] = this->mode == climate::CLIMATE_MODE_OFF ? 0x00 : 0x01;
    payload[2] = this->have_payload_ ? this->last_payload_[2] : 0x00;
    payload[3] = this->have_payload_ ? this->last_payload_[3] : 0x06;
    payload[4] = this->encode_swing_(this->swing_mode);
    payload[5] = this->encode_mode_(this->mode);
    payload[6] = this->encode_special_();
    payload[7] = this->encode_fan_(this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO));
    payload[8] = this->encode_temperature_(this->target_temperature);
    payload[9] = std::isnan(this->current_temperature) ? 0x00 : this->encode_temperature_(this->current_temperature);
    payload[15] = static_cast<uint8_t>((millis() / 3600000UL) % 24);
    payload[16] = static_cast<uint8_t>((millis() / 60000UL) % 60);

    std::array<uint8_t, FRAME_BUFFER_SIZE> frame{};
    frame[0] = HEADER;
    frame[1] = STATUS_LEN;
    std::copy(payload.begin(), payload.end(), frame.begin() + 2);
    frame[21] = this->checksum_(frame.data(), 21);

    this->write_array(frame.data(), 22);
    this->flush();
    ESP_LOGD(TAG, "TX command: power=%u mode=%u fan=%u target=%u special=%02X swing=%02X",
             payload[1], payload[5], payload[7], payload[8], payload[6], payload[4]);
  }

  static uint8_t checksum_(const uint8_t *data, uint8_t size) {
    uint16_t sum = 0;
    for (uint8_t i = 0; i < size; i++)
      sum += data[i];
    return static_cast<uint8_t>(sum & 0xFF);
  }

  static float decode_temperature_(uint8_t value) { return static_cast<float>(value); }

  static uint8_t encode_temperature_(float value) {
    value = std::max(Constants::MIN_TEMPERATURE, std::min(Constants::MAX_TEMPERATURE, value));
    return static_cast<uint8_t>(roundf(value));
  }

  static climate::ClimateMode decode_mode_(uint8_t power, uint8_t mode) {
    if (power == 0x00)
      return climate::CLIMATE_MODE_OFF;
    switch (mode) {
      case 0x00:
        return climate::CLIMATE_MODE_HEAT_COOL;
      case 0x01:
        return climate::CLIMATE_MODE_COOL;
      case 0x02:
        return climate::CLIMATE_MODE_DRY;
      case 0x03:
        return climate::CLIMATE_MODE_HEAT;
      case 0x04:
        return climate::CLIMATE_MODE_FAN_ONLY;
      default:
        return climate::CLIMATE_MODE_HEAT_COOL;
    }
  }

  static uint8_t encode_mode_(climate::ClimateMode mode) {
    switch (mode) {
      case climate::CLIMATE_MODE_COOL:
        return 0x01;
      case climate::CLIMATE_MODE_DRY:
        return 0x02;
      case climate::CLIMATE_MODE_HEAT:
        return 0x03;
      case climate::CLIMATE_MODE_FAN_ONLY:
        return 0x04;
      case climate::CLIMATE_MODE_HEAT_COOL:
      case climate::CLIMATE_MODE_AUTO:
      case climate::CLIMATE_MODE_OFF:
      default:
        return 0x00;
    }
  }

  static climate::ClimateFanMode decode_fan_(uint8_t fan) {
    switch (fan) {
      case 0x01:
        return climate::CLIMATE_FAN_LOW;
      case 0x02:
        return climate::CLIMATE_FAN_MEDIUM;
      case 0x03:
        return climate::CLIMATE_FAN_HIGH;
      case 0x00:
      default:
        return climate::CLIMATE_FAN_AUTO;
    }
  }

  static uint8_t encode_fan_(climate::ClimateFanMode fan) {
    switch (fan) {
      case climate::CLIMATE_FAN_LOW:
        return 0x01;
      case climate::CLIMATE_FAN_MEDIUM:
        return 0x02;
      case climate::CLIMATE_FAN_HIGH:
        return 0x03;
      case climate::CLIMATE_FAN_AUTO:
      default:
        return 0x00;
    }
  }

  static uint8_t encode_swing_(climate::ClimateSwingMode swing) {
    return swing == climate::CLIMATE_SWING_HORIZONTAL ? 0x1A : 0x18;
  }

  uint8_t encode_special_() const {
    uint8_t special = 0x10;
    if (this->have_payload_)
      special |= this->last_payload_[6] & 0x02;
    if (this->preset == climate::CLIMATE_PRESET_SLEEP)
      special |= 0x01;
    const auto custom_fan = this->get_custom_fan_mode();
    if (this->string_ref_equals_(custom_fan, Constants::TURBO))
      special |= 0x08;
    if (this->string_ref_equals_(custom_fan, Constants::QUIET))
      special |= 0x04;
    return special;
  }

  static bool string_ref_equals_(const StringRef &value, const char *expected) {
    return value.size() == strlen(expected) && strncmp(value.c_str(), expected, value.size()) == 0;
  }

  void clamp_frontend_state_() {
    if (this->mode == climate::CLIMATE_MODE_AUTO)
      this->mode = climate::CLIMATE_MODE_HEAT_COOL;
    if (std::isnan(this->target_temperature))
      this->target_temperature = 24.0f;
    this->target_temperature = std::max(Constants::MIN_TEMPERATURE,
                                       std::min(Constants::MAX_TEMPERATURE, this->target_temperature));
  }

  uint32_t period_{5000};
  uint32_t last_poll_{0};
  bool optimistic_{true};

  RxState rx_state_{RX_WAIT_HEADER};
  std::array<uint8_t, FRAME_BUFFER_SIZE> rx_buffer_{};
  uint8_t rx_pos_{0};
  uint8_t rx_expected_{0};

  bool have_payload_{false};
  std::array<uint8_t, PAYLOAD_SIZE> last_payload_{};

  climate::ClimateModeMask supported_modes_{};
  climate::ClimateSwingModeMask supported_swing_modes_{};
  climate::ClimatePresetMask supported_presets_{};
  std::vector<const char *> supported_custom_fan_modes_{Constants::TURBO, Constants::QUIET};

  sensor::Sensor *current_temperature_sensor_{nullptr};
  binary_sensor::BinarySensor *turbo_sensor_{nullptr};
  binary_sensor::BinarySensor *ionizer_sensor_{nullptr};
  binary_sensor::BinarySensor *quiet_sensor_{nullptr};
};

}  // namespace chiq_ac
}  // namespace esphome
