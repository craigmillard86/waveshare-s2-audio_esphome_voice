#pragma once

#include "esphome/core/component.h"
#include "esphome/components/speaker/speaker.h"
#include "esp_afe.h"

namespace esphome {
namespace esp_afe {

// Simple 3:1 decimation filter for 48kHz to 16kHz conversion
class Resampler48to16 {
 public:
  Resampler48to16();

  // Process stereo 48kHz input, output mono 16kHz
  // Returns number of output samples written
  size_t process(const int16_t *input, size_t input_samples,
                 int16_t *output, size_t output_capacity);

  void reset();

 private:
  // Simple FIR lowpass filter coefficients for anti-aliasing
  // Cutoff ~7kHz at 48kHz sample rate
  static const int FILTER_TAPS = 9;
  static const int16_t filter_coeffs_[FILTER_TAPS];

  // Filter state (delay line)
  int16_t delay_line_[FILTER_TAPS];
  int delay_idx_;

  // Decimation counter
  int decimate_count_;
};

// Reference capture wrapper that taps speaker output
// and feeds it to the AFE for AEC processing
class ReferenceCapture : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  // Configuration
  void set_esp_afe(EspAfe *afe) { this->afe_ = afe; }
  void set_speaker(speaker::Speaker *spk) { this->speaker_ = spk; }

  // Called when speaker data is available
  void on_speaker_data(const std::vector<uint8_t> &data);

 protected:
  EspAfe *afe_{nullptr};
  speaker::Speaker *speaker_{nullptr};

  // Resampler for 48kHz -> 16kHz conversion
  Resampler48to16 resampler_;

  // Resampled output buffer
  std::vector<int16_t> resample_output_;
};

}  // namespace esp_afe
}  // namespace esphome
