#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/microphone/microphone.h"
#include "esphome/components/speaker/speaker.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

// ESP-SR headers - must be included before any ESPHome headers that might conflict
#include "esp_afe_sr_iface.h"
#include "esp_afe_config.h"
#include "esp_afe_sr_models.h"

namespace esphome {
namespace esp_afe {

// Forward declarations
class AfeMicrophone;
class ReferenceCapture;

enum AfeMode : uint8_t {
  ESP_AFE_MODE_LOW_COST = 0,
  ESP_AFE_MODE_HIGH_PERF = 1,
};

// AFE task event bits
static const uint32_t AFE_TASK_STARTING = (1 << 0);
static const uint32_t AFE_TASK_RUNNING = (1 << 1);
static const uint32_t AFE_TASK_STOPPED = (1 << 2);
static const uint32_t AFE_COMMAND_STOP = (1 << 3);

class EspAfe : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  // Configuration setters
  void set_microphone_source(microphone::Microphone *mic) { this->mic_source_ = mic; }
  void set_speaker_reference(speaker::Speaker *spk) { this->speaker_ref_ = spk; }
  void set_afe_mode(AfeMode mode) { this->afe_mode_ = mode; }
  void set_aec_enabled(bool enabled) { this->aec_enabled_ = enabled; }
  void set_bss_enabled(bool enabled) { this->bss_enabled_ = enabled; }
  void set_ns_enabled(bool enabled) { this->ns_enabled_ = enabled; }
  void set_vad_enabled(bool enabled) { this->vad_enabled_ = enabled; }
  void set_agc_enabled(bool enabled) { this->agc_enabled_ = enabled; }

  // Register output microphone
  void register_output_microphone(AfeMicrophone *mic) { this->output_mic_ = mic; }

  // AFE state queries
  bool is_running() const { return this->afe_running_; }
  int get_feed_chunksize() const { return this->feed_chunksize_; }
  int get_fetch_chunksize() const { return this->fetch_chunksize_; }

  // Start/stop AFE processing
  void start();
  void stop();

  // Get last fetch result
  afe_fetch_result_t *get_last_result() { return this->last_result_; }

 protected:
  // Initialize AFE
  bool init_afe_();

  // Audio processing task (static for FreeRTOS)
  static void afe_task_(void *param);

  // Callbacks
  void on_mic_data_(const std::vector<uint8_t> &data);
  void on_speaker_data_(const std::vector<uint8_t> &data);

  // Configuration
  microphone::Microphone *mic_source_{nullptr};
  speaker::Speaker *speaker_ref_{nullptr};
  AfeMode afe_mode_{ESP_AFE_MODE_HIGH_PERF};
  bool aec_enabled_{true};
  bool bss_enabled_{false};
  bool ns_enabled_{true};
  bool vad_enabled_{true};
  bool agc_enabled_{true};

  // AFE handles
  const esp_afe_sr_iface_t *afe_handle_{nullptr};
  esp_afe_sr_data_t *afe_data_{nullptr};
  afe_config_t *afe_config_ptr_{nullptr};

  // Processing state
  bool afe_running_{false};
  bool afe_initialized_{false};
  int feed_chunksize_{0};
  int fetch_chunksize_{0};
  int total_channel_num_{0};

  // Audio buffers
  int16_t *feed_buffer_{nullptr};
  size_t feed_buffer_pos_{0};

  // Microphone input ring buffer
  std::vector<int16_t> mic_ring_buffer_;
  size_t mic_ring_write_pos_{0};
  size_t mic_ring_read_pos_{0};
  SemaphoreHandle_t mic_buffer_mutex_{nullptr};

  // Speaker reference ring buffer (for AEC)
  std::vector<int16_t> ref_ring_buffer_;
  size_t ref_ring_write_pos_{0};
  size_t ref_ring_read_pos_{0};
  SemaphoreHandle_t ref_buffer_mutex_{nullptr};

  // Resampler for speaker reference (48kHz -> 16kHz)
  std::vector<int16_t> resample_buffer_;

  // FreeRTOS task handles
  TaskHandle_t afe_task_handle_{nullptr};
  EventGroupHandle_t event_group_{nullptr};
  StackType_t *afe_task_stack_{nullptr};  // Task stack allocated in PSRAM
  StaticTask_t afe_task_tcb_;             // Task control block for static allocation

  // Output microphone
  AfeMicrophone *output_mic_{nullptr};

  // Last fetch result
  afe_fetch_result_t *last_result_{nullptr};
};

}  // namespace esp_afe
}  // namespace esphome
