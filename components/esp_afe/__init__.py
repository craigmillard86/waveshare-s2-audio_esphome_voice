import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import microphone, speaker
from esphome.components.esp32 import add_idf_component
from esphome.const import CONF_ID

CODEOWNERS = ["@your-username"]
DEPENDENCIES = ["microphone", "psram", "esp32"]
AUTO_LOAD = ["microphone"]

CONF_ESP_AFE_ID = "esp_afe_id"
CONF_MICROPHONE_SOURCE = "microphone_source"
CONF_SPEAKER_REFERENCE = "speaker_reference"
CONF_AFE_MODE = "afe_mode"
CONF_AEC_ENABLED = "aec_enabled"
CONF_BSS_ENABLED = "bss_enabled"
CONF_NS_ENABLED = "ns_enabled"
CONF_VAD_ENABLED = "vad_enabled"
CONF_AGC_ENABLED = "agc_enabled"

esp_afe_ns = cg.esphome_ns.namespace("esp_afe")
EspAfe = esp_afe_ns.class_("EspAfe", cg.Component)
AfeMicrophone = esp_afe_ns.class_(
    "AfeMicrophone", microphone.Microphone, cg.Component
)

# AFE Mode enum
AfeMode = esp_afe_ns.enum("AfeMode")
AFE_MODES = {
    "LOW_COST": AfeMode.ESP_AFE_MODE_LOW_COST,
    "HIGH_PERF": AfeMode.ESP_AFE_MODE_HIGH_PERF,
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EspAfe),
        cv.Required(CONF_MICROPHONE_SOURCE): cv.use_id(microphone.Microphone),
        cv.Optional(CONF_SPEAKER_REFERENCE): cv.use_id(speaker.Speaker),
        cv.Optional(CONF_AFE_MODE, default="HIGH_PERF"): cv.enum(
            AFE_MODES, upper=True
        ),
        cv.Optional(CONF_AEC_ENABLED, default=True): cv.boolean,
        cv.Optional(CONF_BSS_ENABLED, default=False): cv.boolean,
        cv.Optional(CONF_NS_ENABLED, default=True): cv.boolean,
        cv.Optional(CONF_VAD_ENABLED, default=True): cv.boolean,
        cv.Optional(CONF_AGC_ENABLED, default=True): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    mic = await cg.get_variable(config[CONF_MICROPHONE_SOURCE])
    cg.add(var.set_microphone_source(mic))

    if CONF_SPEAKER_REFERENCE in config:
        spk = await cg.get_variable(config[CONF_SPEAKER_REFERENCE])
        cg.add(var.set_speaker_reference(spk))

    cg.add(var.set_afe_mode(config[CONF_AFE_MODE]))
    cg.add(var.set_aec_enabled(config[CONF_AEC_ENABLED]))
    cg.add(var.set_bss_enabled(config[CONF_BSS_ENABLED]))
    cg.add(var.set_ns_enabled(config[CONF_NS_ENABLED]))
    cg.add(var.set_vad_enabled(config[CONF_VAD_ENABLED]))
    cg.add(var.set_agc_enabled(config[CONF_AGC_ENABLED]))

    # Add esp-sr as an ESP-IDF component from GitHub
    # The esp-sr library provides AFE (Audio Front End) processing
    # v2.0.0 is required for ESP-IDF 5.x compatibility
    add_idf_component(
        name="esp-sr",
        repo="https://github.com/espressif/esp-sr",
        ref="v2.0.0",
    )

    # Add required build flags for PSRAM usage
    cg.add_build_flag("-DBOARD_HAS_PSRAM")
