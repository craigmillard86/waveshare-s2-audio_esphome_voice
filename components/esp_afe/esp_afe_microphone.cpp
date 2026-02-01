#include "esp_afe_microphone.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esp_afe {

static const char *const TAG = "esp_afe.microphone";

void AfeMicrophone::setup() {
  if (this->afe_ == nullptr) {
    ESP_LOGE(TAG, "No AFE component configured!");
    this->mark_failed();
    return;
  }

  // Register this microphone with the AFE
  this->afe_->register_output_microphone(this);
}

void AfeMicrophone::loop() {
  // State management is handled by the base class
  // Audio data is pushed via publish_audio() from the AFE task
}

void AfeMicrophone::dump_config() {
  ESP_LOGCONFIG(TAG, "AFE Microphone:");
  ESP_LOGCONFIG(TAG, "  Sample Rate: 16000 Hz");
  ESP_LOGCONFIG(TAG, "  Bits Per Sample: 16");
  ESP_LOGCONFIG(TAG, "  Channels: 1 (mono)");
  if (this->is_failed()) {
    ESP_LOGCONFIG(TAG, "  Setup failed!");
  }
}

void AfeMicrophone::start() {
  if (this->is_failed()) {
    return;
  }

  if (this->state_ == microphone::STATE_RUNNING) {
    return;
  }

  this->state_ = microphone::STATE_STARTING;

  // Start the AFE processing
  if (this->afe_ != nullptr) {
    this->afe_->start();
  }

  this->state_ = microphone::STATE_RUNNING;
}

void AfeMicrophone::stop() {
  if (this->state_ == microphone::STATE_STOPPED) {
    return;
  }

  this->state_ = microphone::STATE_STOPPING;

  // Stop the AFE processing
  if (this->afe_ != nullptr) {
    this->afe_->stop();
  }

  this->state_ = microphone::STATE_STOPPED;
}

void AfeMicrophone::publish_audio(const std::vector<uint8_t> &data) {
  // Only publish if running - this is called from AFE task context
  if (this->state_ != microphone::STATE_RUNNING) {
    return;
  }

  // Validate data size
  if (data.empty() || data.size() > 4096) {
    return;
  }

  // Check if muted
  if (this->mute_state_) {
    // Send zeros instead of actual audio
    std::vector<uint8_t> silence(data.size(), 0);
    this->data_callbacks_.call(silence);
  } else {
    // Pass through the AFE-processed audio
    this->data_callbacks_.call(data);
  }
}

}  // namespace esp_afe
}  // namespace esphome
