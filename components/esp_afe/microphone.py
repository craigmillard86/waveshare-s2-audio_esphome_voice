import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import microphone, audio
from esphome.const import CONF_ID

from . import esp_afe_ns, EspAfe, CONF_ESP_AFE_ID

CODEOWNERS = ["@your-username"]
DEPENDENCIES = ["esp_afe"]

AfeMicrophone = esp_afe_ns.class_(
    "AfeMicrophone", microphone.Microphone, cg.Component
)


def set_afe_audio_limits(config):
    """Set audio stream limits for AFE microphone (16-bit, mono, 16kHz)"""
    config[audio.CONF_MIN_BITS_PER_SAMPLE] = 16
    config[audio.CONF_MAX_BITS_PER_SAMPLE] = 16
    config[audio.CONF_MIN_CHANNELS] = 1
    config[audio.CONF_MAX_CHANNELS] = 1
    config[audio.CONF_MIN_SAMPLE_RATE] = 16000
    config[audio.CONF_MAX_SAMPLE_RATE] = 16000
    return config


# AFE outputs fixed 16kHz mono 16-bit audio
CONFIG_SCHEMA = cv.All(
    microphone.MICROPHONE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(AfeMicrophone),
            cv.GenerateID(CONF_ESP_AFE_ID): cv.use_id(EspAfe),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    set_afe_audio_limits,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await microphone.register_microphone(var, config)

    # Set audio stream info: 16-bit, 1 channel, 16kHz
    cg.add(var.set_audio_stream_info(16, 1, 16000))

    afe = await cg.get_variable(config[CONF_ESP_AFE_ID])
    cg.add(var.set_esp_afe(afe))
