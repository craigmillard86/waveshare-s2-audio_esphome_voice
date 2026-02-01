#pragma once

#include "esphome/core/component.h"
#include "esphome/components/microphone/microphone.h"
#include "esphome/components/audio/audio.h"
#include "esp_afe.h"

namespace esphome {
namespace esp_afe {

class AfeMicrophone : public microphone::Microphone, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Microphone interface
  void start() override;
  void stop() override;

  // Configuration
  void set_esp_afe(EspAfe *afe) { this->afe_ = afe; }

  // Set audio stream info (bits_per_sample, channels, sample_rate)
  void set_audio_stream_info(uint8_t bits_per_sample, uint8_t channels, uint32_t sample_rate) {
    this->audio_stream_info_ = audio::AudioStreamInfo(bits_per_sample, channels, sample_rate);
  }

  // Called by EspAfe to publish processed audio
  void publish_audio(const std::vector<uint8_t> &data);

 protected:
  EspAfe *afe_{nullptr};
};

}  // namespace esp_afe
}  // namespace esphome
