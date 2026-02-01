#include "reference_capture.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esp_afe {

static const char *const TAG = "esp_afe.reference";

// Simple lowpass FIR filter coefficients
// Designed for cutoff around 7kHz at 48kHz sample rate
// These provide reasonable anti-aliasing for 3:1 decimation
const int16_t Resampler48to16::filter_coeffs_[Resampler48to16::FILTER_TAPS] = {
    512, 1024, 2048, 3072, 3584, 3072, 2048, 1024, 512
};
// Sum of coefficients = 16896, so we'll divide by 16384 (shift right 14)

Resampler48to16::Resampler48to16() {
  reset();
}

void Resampler48to16::reset() {
  for (int i = 0; i < FILTER_TAPS; i++) {
    delay_line_[i] = 0;
  }
  delay_idx_ = 0;
  decimate_count_ = 0;
}

size_t Resampler48to16::process(const int16_t *input, size_t input_samples,
                                 int16_t *output, size_t output_capacity) {
  size_t output_count = 0;

  for (size_t i = 0; i < input_samples && output_count < output_capacity; i++) {
    // Store sample in delay line
    delay_line_[delay_idx_] = input[i];
    delay_idx_ = (delay_idx_ + 1) % FILTER_TAPS;

    // Decimate: output 1 sample for every 3 input samples
    decimate_count_++;
    if (decimate_count_ >= 3) {
      decimate_count_ = 0;

      // Apply FIR filter
      int32_t acc = 0;
      int idx = delay_idx_;
      for (int j = 0; j < FILTER_TAPS; j++) {
        acc += (int32_t)delay_line_[idx] * (int32_t)filter_coeffs_[j];
        idx = (idx + 1) % FILTER_TAPS;
      }

      // Scale output (divide by sum of coefficients)
      int16_t out_sample = (int16_t)(acc >> 14);
      output[output_count++] = out_sample;
    }
  }

  return output_count;
}

void ReferenceCapture::setup() {
  ESP_LOGI(TAG, "Setting up Reference Capture...");

  if (this->afe_ == nullptr) {
    ESP_LOGE(TAG, "No AFE component configured!");
    this->mark_failed();
    return;
  }

  // Pre-allocate resample buffer
  // Max input: 48kHz stereo at 100ms = 9600 samples
  // After resampling: 16kHz mono at 100ms = 1600 samples
  this->resample_output_.resize(1600);

  ESP_LOGI(TAG, "Reference Capture setup complete");
}

void ReferenceCapture::loop() {
  // Nothing to do here - data is pushed via callback
}

void ReferenceCapture::dump_config() {
  ESP_LOGCONFIG(TAG, "Reference Capture:");
  ESP_LOGCONFIG(TAG, "  Input: 48kHz stereo");
  ESP_LOGCONFIG(TAG, "  Output: 16kHz mono");
  if (this->is_failed()) {
    ESP_LOGCONFIG(TAG, "  Setup failed!");
  }
}

void ReferenceCapture::on_speaker_data(const std::vector<uint8_t> &data) {
  if (this->afe_ == nullptr || !this->afe_->is_running()) {
    return;
  }

  // Input is assumed to be 48kHz stereo 16-bit
  const int16_t *stereo_samples = reinterpret_cast<const int16_t *>(data.data());
  size_t stereo_sample_count = data.size() / sizeof(int16_t);
  size_t frame_count = stereo_sample_count / 2;  // Stereo frames

  // Mix stereo to mono first
  std::vector<int16_t> mono_48k(frame_count);
  for (size_t i = 0; i < frame_count; i++) {
    int32_t left = stereo_samples[i * 2];
    int32_t right = stereo_samples[i * 2 + 1];
    mono_48k[i] = (int16_t)((left + right) / 2);
  }

  // Resample 48kHz mono to 16kHz mono
  size_t resampled_count = this->resampler_.process(
      mono_48k.data(), frame_count,
      this->resample_output_.data(), this->resample_output_.size());

  if (resampled_count > 0) {
    // Convert to byte vector and send to AFE
    std::vector<uint8_t> ref_data(
        reinterpret_cast<uint8_t *>(this->resample_output_.data()),
        reinterpret_cast<uint8_t *>(this->resample_output_.data() + resampled_count));

    // Note: We need a way to pass this to the AFE
    // This would be called from within the speaker's data output path
    // For now, the AFE has on_speaker_data_ that we should wire up
  }
}

}  // namespace esp_afe
}  // namespace esphome
