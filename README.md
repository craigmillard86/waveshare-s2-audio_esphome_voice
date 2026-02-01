# Waveshare ESP32-S3 Audio Board - ESPHome Voice Assistant

ESPHome configuration for the [Waveshare ESP32-S3 Audio Board](https://www.waveshare.com/esp32-s3-audio-board.htm) as a Home Assistant Voice Satellite with advanced features.

## Features

- **Voice Assistant** - Full Home Assistant voice assistant integration
- **On-device Wake Word** - Multiple wake words (Okay Nabu, Hey Jarvis, Alexa, Kenobi, Stop)
- **Dual Microphone** - ES7210 ADC with noise suppression via ESP-SR Audio Front End
- **High Quality Audio** - ES8311 DAC with simultaneous music and announcements
- **LED Status Ring** - 7x WS2812 RGB LEDs with animated effects for each assistant phase
- **Physical Controls** - 3 configurable buttons with single/double/long press actions
- **Timers & Alarms** - Built-in timer and alarm functionality
- **Media Player** - Exposed to Home Assistant for music and announcements
- **Presence Detection** - Optional LD2410C mmWave radar for privacy-focused mic control

## Hardware

### Waveshare ESP32-S3 Audio Board

| Component | Chip | Interface |
|-----------|------|-----------|
| MCU | ESP32-S3 (16MB Flash, 8MB PSRAM) | - |
| Audio DAC | ES8311 | I2C + I2S |
| Audio ADC | ES7210 (dual mic) | I2C + I2S |
| I/O Expander | TCA9555 | I2C |
| RTC | PCF85063 | I2C |
| LED Ring | WS2812 x7 | GPIO38 |

### Optional: LD2410C Presence Sensor

| Spec | Value |
|------|-------|
| Type | 24GHz mmWave Radar |
| Interface | UART @ 256000 baud |
| Power | 5V, ~200mA |
| Range | 0.3m - 5.5m |

## GPIO Pinout

```
ESP32-S3 Pin Assignments
========================

I2S Audio Bus (shared DAC/ADC):
  GPIO12  MCLK
  GPIO13  BCLK/SCLK
  GPIO14  LRCLK/WS
  GPIO15  DIN (mic data)
  GPIO16  DOUT (speaker data)

I2C Bus:
  GPIO10  SCL
  GPIO11  SDA

LED Ring:
  GPIO38  WS2812 Data

LD2410C Presence (Optional):
  GPIO5   RX (from LD2410 TX)
  GPIO6   TX (to LD2410 RX)

Reserved/Unavailable:
  GPIO19-20  USB
  GPIO26-37  Flash/PSRAM
```

## File Structure

```
waveshare-s3-audio.yaml          # Main configuration
packages/
├── audio_hardware.yaml          # I2S, codecs, microphone, speaker, media player
├── voice_assistant.yaml         # Voice assistant and wake word config
├── led_control.yaml             # LED ring effects and status display
├── buttons_controls.yaml        # Button handling and mic controls
├── timers_alarms.yaml           # Timer and alarm functionality
└── presence_detection.yaml      # LD2410C mmWave radar (optional)
components/
└── esp_afe/                     # Custom ESP-SR Audio Front End component
```

## Installation

### Prerequisites

- [ESPHome](https://esphome.io/) 2024.8.0 or later
- Home Assistant with Voice Assistant configured
- WiFi network credentials

### Setup

1. **Clone the repository:**
   ```bash
   git clone https://github.com/craigmillard86/waveshare-s2-audio_esphome_voice.git
   cd waveshare-s2-audio_esphome_voice
   ```

2. **Create secrets.yaml:**
   ```yaml
   wifi_ssid: "YourWiFiSSID"
   wifi_password: "YourWiFiPassword"
   api_esp32-audio-s3: "your-32-character-api-key-here!"
   ota_password: "your-ota-password"
   ```

3. **Compile and upload:**
   ```bash
   esphome run waveshare-s3-audio.yaml
   ```

4. **Add to Home Assistant:**
   - The device will be auto-discovered
   - Configure the Voice Assistant in Home Assistant

## Configuration

### Wake Words

Multiple wake words are configured with adjustable sensitivity:

| Wake Word | Default Cutoff | Notes |
|-----------|---------------|-------|
| Okay Nabu | 0.20 | Primary, most sensitive |
| Hey Jarvis | 0.97 | Requires clear pronunciation |
| Alexa | 0.90 | Standard sensitivity |
| Kenobi | 0.90 | Fun alternative |
| Stop | 0.50 | For canceling operations |

Adjust sensitivity via the **Wake Word Sensitivity** select in Home Assistant.

### Button Actions

Each button supports single press, double press, and long press with configurable actions:

| Action | Description |
|--------|-------------|
| Volume Up/Down | Adjust media player volume |
| Mute Toggle | Mute/unmute media player |
| Play/Pause | Toggle media playback |
| Next/Previous Track | Skip tracks (sends HA event) |
| Start Voice Assistant | Trigger voice assistant |
| Toggle Voice Assistant | Enable/disable wake word detection |
| Send HA Event | Fire custom event for automations |

### Audio Settings

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| Microphone Gain | 0-36 dB | 32 dB | ES7210 hardware gain |
| Voice Boost | 1-64x | 32x | Software gain multiplier |
| Noise Reduction | 0-4 | 4 | Voice assistant noise suppression |

### LED Effects

The LED ring displays different effects based on system state:

| State | Effect | Color |
|-------|--------|-------|
| Idle (ready) | Off | - |
| Listening | Swirl | Blue |
| Processing | Swirl | Purple |
| Responding | Swirl | Cyan |
| Error | Pulse | Red |
| No WiFi | Pulse | Red |
| No API | Pulse | Yellow |
| Timer Ringing | Rainbow | Multi |
| Volume Change | Gradient | Green→Red |

## Presence Detection (LD2410C)

### Wiring

```
LD2410C          ESP32-S3
───────          ────────
VCC (Pin 5)  →   5V
GND (Pin 4)  →   GND
TX  (Pin 1)  →   GPIO5 (ESP RX)
RX  (Pin 2)  →   GPIO6 (ESP TX)
OUT (Pin 3)  →   (not used)
```

### Behavior

The presence detection module is **optional** - the device works normally without it connected.

| Switch State | Presence Sensors | Microphone |
|--------------|------------------|------------|
| Module not connected | Unknown/unavailable | Works normally |
| Control OFF | Report to HA | Manual control |
| Control ON | Report to HA | Auto-mute when empty |

When **"Presence Controls Mic"** is enabled:
1. **Room occupied** → Microphone enabled
2. **Room empty for 30s** → Microphone muted (privacy)
3. **Person returns** → Microphone re-enabled immediately

### Calibration

1. Enable **"Radar Engineering Mode"** switch
2. Observe energy levels in Home Assistant
3. Adjust thresholds:
   - **Max Move Distance Gate**: Motion detection range (0-8, each = 0.75m)
   - **Max Still Distance Gate**: Stationary presence range
   - **Presence Timeout**: Seconds after last detection

## Home Assistant Entities

### Sensors
- Room Presence, Moving Target, Still Target (binary)
- Moving/Still Distance, Energy, Detection Distance
- Timer Remaining, Active Timer
- Device Time, Alarm Time

### Controls
- Voice Assistant (on/off)
- Microphone Mute
- Presence Controls Mic
- Feedback Sounds, Wake Sound Effect
- LED Ring Enabled
- Amplifier
- Alarm Enabled

### Configuration
- Wake Word Sensitivity
- Button Actions (9 configurable)
- Microphone Gain, Voice Boost, Noise Reduction
- LED Ring Brightness
- Radar Engineering Mode, Bluetooth
- Presence Timeout, Distance Gates

## ESP-SR Audio Front End

This project includes a custom `esp_afe` component that provides:

- **Dual Microphone Support** - Stereo input from ES7210
- **Noise Suppression (NS)** - Reduces background noise
- **Auto Gain Control (AGC)** - Consistent audio levels
- **Lazy Initialization** - Optimizes RAM usage by initializing on first use

Configuration in `audio_hardware.yaml`:
```yaml
esp_afe:
  id: audio_frontend
  microphone_source: i2s_mics
  afe_mode: LOW_COST
  aec_enabled: false
  bss_enabled: false
  ns_enabled: true
  vad_enabled: false
  agc_enabled: true
```

## Troubleshooting

### No audio output
- Check **Amplifier** switch is ON
- Verify volume is not 0
- Check I2C scan shows device at 0x18 (ES8311)

### Wake word not detecting
- Check **Voice Assistant** switch is ON
- Increase **Wake Word Sensitivity**
- Check **Microphone Mute** is OFF
- Verify **Microphone Gain** is adequate (try 32 dB)

### Voice not understood
- Increase **Voice Boost** (try 32x)
- Adjust **Noise Reduction** level
- Check Home Assistant STT configuration

### Presence sensor not working
- Verify UART in logs: `UART Bus 0: TX Pin: GPIO6, RX Pin: GPIO5`
- Check firmware version is not `0.00.00000000` (indicates not connected)
- Ensure 5V power to LD2410C (not 3.3V)

### High RAM usage
- ESP-SR AFE uses significant RAM
- PSRAM is required and must be configured as `octal` mode
- BLE is disabled to save internal RAM

## Credits

- Original configuration by [sw3Dan](https://github.com/sw3Dan/waveshare-s2-audio_esphome_voice)
- ES8311 I2S master mode patch by sw3Dan
- ESP-SR AFE integration and modular refactor by this fork

## License

This project is provided as-is for personal use. See the original repository for license terms.
