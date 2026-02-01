#include "esp_afe.h"
#include "esp_afe_microphone.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"
#include <cstring>

namespace esphome {
namespace esp_afe {

static const char *const TAG = "esp_afe";

// Ring buffer size in samples (500ms at 16kHz stereo = 16000 samples)
static const size_t MIC_RING_BUFFER_SIZE = 16000;
// Reference buffer size (500ms at 16kHz mono)
static const size_t REF_RING_BUFFER_SIZE = 8000;

// AFE task configuration
static const uint32_t AFE_TASK_STACK_SIZE = 8 * 1024;
static const UBaseType_t AFE_TASK_PRIORITY = 12;  // Higher priority for lower latency
static const BaseType_t AFE_TASK_CORE = 0;

void EspAfe::setup() {
  // Create event group
  this->event_group_ = xEventGroupCreate();
  if (this->event_group_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create event group");
    this->mark_failed();
    return;
  }

  // Create mutexes for ring buffers
  this->mic_buffer_mutex_ = xSemaphoreCreateMutex();
  this->ref_buffer_mutex_ = xSemaphoreCreateMutex();
  if (this->mic_buffer_mutex_ == nullptr || this->ref_buffer_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create buffer mutexes");
    this->mark_failed();
    return;
  }

  // Allocate ring buffers in PSRAM
  this->mic_ring_buffer_.resize(MIC_RING_BUFFER_SIZE, 0);
  this->ref_ring_buffer_.resize(REF_RING_BUFFER_SIZE, 0);

  // LAZY INITIALIZATION: Don't initialize AFE here!
  // AFE uses ~130KB of internal RAM during init. By deferring init to start(),
  // we allow I2S DMA buffers to be allocated first (they MUST be in internal RAM).
  // AFE will be initialized on first call to start().

  // Register microphone callback
  if (this->mic_source_ != nullptr) {
    this->mic_source_->add_data_callback([this](const std::vector<uint8_t> &data) {
      this->on_mic_data_(data);
    });
  }
}

bool EspAfe::init_afe_() {
  // Determine input format based on AEC setting
  // Format: M=mic, R=reference, N=unused
  const char *input_format;
  if (this->aec_enabled_ && this->speaker_ref_ != nullptr) {
    // 2 mics + 1 reference = "MMR"
    input_format = "MMR";
  } else {
    // 2 mics, no reference = "MM" (dual mic for beamforming)
    input_format = "MM";
  }

  // Determine AFE mode
  afe_mode_t mode = (this->afe_mode_ == ESP_AFE_MODE_HIGH_PERF)
                      ? AFE_MODE_HIGH_PERF
                      : AFE_MODE_LOW_COST;

  // Initialize config using v2.0.0 API helper function
  // Pass NULL for models to use default models from partition
  this->afe_config_ptr_ = afe_config_init(input_format, nullptr, AFE_TYPE_SR, mode);
  if (this->afe_config_ptr_ == nullptr) {
    ESP_LOGE(TAG, "Failed to initialize AFE config");
    return false;
  }

  // Customize based on user settings
  this->afe_config_ptr_->aec_init = this->aec_enabled_ && this->speaker_ref_ != nullptr;
  this->afe_config_ptr_->se_init = this->bss_enabled_;
  this->afe_config_ptr_->vad_init = this->vad_enabled_;
  this->afe_config_ptr_->ns_init = this->ns_enabled_;
  this->afe_config_ptr_->agc_init = this->agc_enabled_;
  this->afe_config_ptr_->wakenet_init = false;  // We use micro_wake_word instead
  this->afe_config_ptr_->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

  // Get AFE handle from config (v2.0.0 API)
  this->afe_handle_ = esp_afe_handle_from_config(this->afe_config_ptr_);
  if (this->afe_handle_ == nullptr) {
    ESP_LOGE(TAG, "Failed to get AFE handle");
    return false;
  }

  // Create AFE data using the handle
  this->afe_data_ = this->afe_handle_->create_from_config(this->afe_config_ptr_);
  if (this->afe_data_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create AFE data");
    return false;
  }

  // Get chunk sizes
  this->feed_chunksize_ = this->afe_handle_->get_feed_chunksize(this->afe_data_);
  this->fetch_chunksize_ = this->afe_handle_->get_fetch_chunksize(this->afe_data_);
  this->total_channel_num_ = this->afe_handle_->get_feed_channel_num(this->afe_data_);

  // Allocate feed buffer in PSRAM
  size_t feed_buffer_bytes = this->feed_chunksize_ * this->total_channel_num_ * sizeof(int16_t);
  this->feed_buffer_ = (int16_t *)heap_caps_aligned_calloc(
      16, 1, feed_buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->feed_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate feed buffer (%d bytes)", feed_buffer_bytes);
    this->afe_handle_->destroy(this->afe_data_);
    return false;
  }

  this->afe_initialized_ = true;
  return true;
}

void EspAfe::loop() {
  // Main loop checks if we should start/stop the AFE task
  // The actual processing happens in the FreeRTOS task
}

void EspAfe::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP-SR AFE:");
  ESP_LOGCONFIG(TAG, "  Mode: %s",
                this->afe_mode_ == ESP_AFE_MODE_HIGH_PERF ? "HIGH_PERF" : "LOW_COST");
  ESP_LOGCONFIG(TAG, "  AEC: %s", YESNO(this->aec_enabled_));
  ESP_LOGCONFIG(TAG, "  BSS: %s", YESNO(this->bss_enabled_));
  ESP_LOGCONFIG(TAG, "  VAD: %s", YESNO(this->vad_enabled_));
  ESP_LOGCONFIG(TAG, "  NS: %s", YESNO(this->ns_enabled_));
  ESP_LOGCONFIG(TAG, "  AGC: %s", YESNO(this->agc_enabled_));
  if (this->afe_initialized_) {
    ESP_LOGCONFIG(TAG, "  Feed chunk: %d samples x %d channels",
                  this->feed_chunksize_, this->total_channel_num_);
    ESP_LOGCONFIG(TAG, "  Fetch chunk: %d samples", this->fetch_chunksize_);
  } else {
    ESP_LOGCONFIG(TAG, "  Initialization: LAZY (will init on first start)");
  }
  if (this->is_failed()) {
    ESP_LOGCONFIG(TAG, "  Setup failed!");
  }
}

void EspAfe::start() {
  if (this->afe_running_) {
    return;
  }

  // Lazy initialization: initialize AFE on first start
  // This ensures I2S DMA buffers are allocated before AFE consumes internal RAM
  if (!this->afe_initialized_) {
    if (!this->init_afe_()) {
      ESP_LOGE(TAG, "Failed to initialize AFE on first start");
      return;
    }
  }

  // Start the source microphone first (if not already running)
  if (this->mic_source_ != nullptr && !this->mic_source_->is_running()) {
    this->mic_source_->start();
  }

  // Clear all event bits before starting
  xEventGroupClearBits(this->event_group_, AFE_COMMAND_STOP | AFE_TASK_RUNNING | AFE_TASK_STOPPED | AFE_TASK_STARTING);

  // Reset ring buffer positions for clean start
  this->mic_ring_write_pos_ = 0;
  this->mic_ring_read_pos_ = 0;
  this->ref_ring_write_pos_ = 0;
  this->ref_ring_read_pos_ = 0;

  // Allocate task stack in PSRAM to avoid internal RAM pressure
  if (this->afe_task_stack_ == nullptr) {
    this->afe_task_stack_ = (StackType_t *)heap_caps_malloc(
        AFE_TASK_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (this->afe_task_stack_ == nullptr) {
      ESP_LOGE(TAG, "Failed to allocate AFE task stack in PSRAM");
      return;
    }
  }

  // Clear TCB memory before reuse (required for static task recreation)
  memset(&this->afe_task_tcb_, 0, sizeof(this->afe_task_tcb_));

  // Create the AFE processing task with static allocation (stack in PSRAM)
  this->afe_task_handle_ = xTaskCreateStaticPinnedToCore(
      EspAfe::afe_task_,
      "afe_task",
      AFE_TASK_STACK_SIZE,
      this,
      AFE_TASK_PRIORITY,
      this->afe_task_stack_,
      &this->afe_task_tcb_,
      AFE_TASK_CORE);

  if (this->afe_task_handle_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create AFE task");
    return;
  }

  // Wait for task to start
  EventBits_t bits = xEventGroupWaitBits(
      this->event_group_,
      AFE_TASK_RUNNING,
      pdFALSE,
      pdTRUE,
      pdMS_TO_TICKS(1000));

  if (bits & AFE_TASK_RUNNING) {
    this->afe_running_ = true;
  } else {
    ESP_LOGE(TAG, "AFE task failed to start");
  }
}

void EspAfe::stop() {
  if (!this->afe_running_) {
    return;
  }

  // Signal task to stop
  xEventGroupSetBits(this->event_group_, AFE_COMMAND_STOP);

  // Wait for task to stop
  EventBits_t bits = xEventGroupWaitBits(
      this->event_group_,
      AFE_TASK_STOPPED,
      pdTRUE,
      pdTRUE,
      pdMS_TO_TICKS(2000));

  if (bits & AFE_TASK_STOPPED) {
    this->afe_running_ = false;
    this->afe_task_handle_ = nullptr;
  }

  // Stop the source microphone (if running)
  if (this->mic_source_ != nullptr && this->mic_source_->is_running()) {
    this->mic_source_->stop();
  }
}

void EspAfe::afe_task_(void *param) {
  EspAfe *afe = static_cast<EspAfe *>(param);

  xEventGroupSetBits(afe->event_group_, AFE_TASK_RUNNING);

  int mic_num = afe->afe_config_ptr_->pcm_config.mic_num;
  int ref_num = afe->afe_config_ptr_->pcm_config.ref_num;
  int total_ch = afe->total_channel_num_;

  while (!(xEventGroupGetBits(afe->event_group_) & AFE_COMMAND_STOP)) {
    // Check if we have enough microphone data
    size_t mic_available = 0;
    if (xSemaphoreTake(afe->mic_buffer_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (afe->mic_ring_write_pos_ >= afe->mic_ring_read_pos_) {
        mic_available = afe->mic_ring_write_pos_ - afe->mic_ring_read_pos_;
      } else {
        mic_available = MIC_RING_BUFFER_SIZE - afe->mic_ring_read_pos_ + afe->mic_ring_write_pos_;
      }
      xSemaphoreGive(afe->mic_buffer_mutex_);
    }

    // Need enough samples for one feed chunk (stereo mic input)
    size_t samples_needed = afe->feed_chunksize_ * mic_num;

    if (mic_available < samples_needed) {
      // Not enough data yet, wait a bit (reduced from 10ms for lower latency)
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }

    // Prepare feed buffer with interleaved channels
    // Format: [ch0_sample0, ch1_sample0, ch2_sample0, ch0_sample1, ch1_sample1, ch2_sample1, ...]

    if (xSemaphoreTake(afe->mic_buffer_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      for (int i = 0; i < afe->feed_chunksize_; i++) {
        int base_idx = i * total_ch;

        // Read mic samples - mono or stereo depending on mic_num
        int16_t mic_sample = afe->mic_ring_buffer_[afe->mic_ring_read_pos_];
        afe->mic_ring_read_pos_ = (afe->mic_ring_read_pos_ + 1) % MIC_RING_BUFFER_SIZE;

        // Fill first mic channel
        afe->feed_buffer_[base_idx + 0] = mic_sample;

        // If stereo mode, read and fill second mic channel
        if (mic_num > 1) {
          int16_t mic_right = afe->mic_ring_buffer_[afe->mic_ring_read_pos_];
          afe->mic_ring_read_pos_ = (afe->mic_ring_read_pos_ + 1) % MIC_RING_BUFFER_SIZE;
          afe->feed_buffer_[base_idx + 1] = mic_right;
        }

        // Fill reference channel if AEC is enabled
        if (ref_num > 0) {
          int16_t ref_sample = 0;
          if (xSemaphoreTake(afe->ref_buffer_mutex_, 0) == pdTRUE) {
            if (afe->ref_ring_write_pos_ != afe->ref_ring_read_pos_) {
              ref_sample = afe->ref_ring_buffer_[afe->ref_ring_read_pos_];
              afe->ref_ring_read_pos_ = (afe->ref_ring_read_pos_ + 1) % REF_RING_BUFFER_SIZE;
            }
            xSemaphoreGive(afe->ref_buffer_mutex_);
          }
          afe->feed_buffer_[base_idx + mic_num] = ref_sample;
        }
      }
      xSemaphoreGive(afe->mic_buffer_mutex_);
    }

    // Feed audio to AFE
    afe->afe_handle_->feed(afe->afe_data_, afe->feed_buffer_);

    // Fetch processed result
    afe_fetch_result_t *result = afe->afe_handle_->fetch(afe->afe_data_);

    if (result != nullptr && result->ret_value == 0) {
      afe->last_result_ = result;

      // Pass processed audio to output microphone
      if (afe->output_mic_ != nullptr && result->data != nullptr && result->data_size > 0) {
        // Sanity check data_size (should be fetch_chunksize * sizeof(int16_t))
        size_t expected_size = afe->fetch_chunksize_ * sizeof(int16_t);
        if (result->data_size > expected_size * 2) {
          continue;
        }

        // Convert to vector of bytes
        std::vector<uint8_t> audio_data(
            reinterpret_cast<uint8_t *>(result->data),
            reinterpret_cast<uint8_t *>(result->data) + result->data_size);
        afe->output_mic_->publish_audio(audio_data);
      }
    }
  }

  xEventGroupClearBits(afe->event_group_, AFE_TASK_RUNNING);
  xEventGroupSetBits(afe->event_group_, AFE_TASK_STOPPED);
  vTaskDelete(nullptr);
}

void EspAfe::on_mic_data_(const std::vector<uint8_t> &data) {
  if (!this->afe_running_) {
    return;
  }

  // Data is 16-bit samples
  const int16_t *samples = reinterpret_cast<const int16_t *>(data.data());
  size_t sample_count = data.size() / sizeof(int16_t);

  if (xSemaphoreTake(this->mic_buffer_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    for (size_t i = 0; i < sample_count; i++) {
      this->mic_ring_buffer_[this->mic_ring_write_pos_] = samples[i];
      this->mic_ring_write_pos_ = (this->mic_ring_write_pos_ + 1) % MIC_RING_BUFFER_SIZE;

      // Check for overflow (write catching up to read)
      if (this->mic_ring_write_pos_ == this->mic_ring_read_pos_) {
        // Advance read position to drop oldest samples
        this->mic_ring_read_pos_ = (this->mic_ring_read_pos_ + 1) % MIC_RING_BUFFER_SIZE;
      }
    }
    xSemaphoreGive(this->mic_buffer_mutex_);
  }
}

void EspAfe::on_speaker_data_(const std::vector<uint8_t> &data) {
  if (!this->afe_running_ || !this->aec_enabled_) {
    return;
  }

  // Speaker data might be at different sample rate (48kHz)
  // For now, assume it's already at 16kHz mono
  // TODO: Add proper resampling if needed

  const int16_t *samples = reinterpret_cast<const int16_t *>(data.data());
  size_t sample_count = data.size() / sizeof(int16_t);

  if (xSemaphoreTake(this->ref_buffer_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    for (size_t i = 0; i < sample_count; i++) {
      this->ref_ring_buffer_[this->ref_ring_write_pos_] = samples[i];
      this->ref_ring_write_pos_ = (this->ref_ring_write_pos_ + 1) % REF_RING_BUFFER_SIZE;

      // Check for overflow
      if (this->ref_ring_write_pos_ == this->ref_ring_read_pos_) {
        this->ref_ring_read_pos_ = (this->ref_ring_read_pos_ + 1) % REF_RING_BUFFER_SIZE;
      }
    }
    xSemaphoreGive(this->ref_buffer_mutex_);
  }
}

}  // namespace esp_afe
}  // namespace esphome
