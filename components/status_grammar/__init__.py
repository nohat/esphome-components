import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import output
from esphome.const import CONF_ID

DEPENDENCIES = ["api", "output", "wifi"]

CONF_NORMAL_OUTPUT = "normal_output"
CONF_EXCEPTION_OUTPUT = "exception_output"
CONF_NORMAL_MAX_POWER = "normal_max_power"
CONF_EXCEPTION_MAX_POWER = "exception_max_power"
CONF_IDLE_BRIGHTNESS = "idle_brightness"
CONF_RENDER_INTERVAL = "render_interval"

ns = cg.esphome_ns.namespace("status_grammar")
StatusGrammar = ns.class_("StatusGrammar", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(StatusGrammar),
    cv.Required(CONF_NORMAL_OUTPUT): cv.use_id(output.FloatOutput),
    cv.Optional(CONF_EXCEPTION_OUTPUT): cv.use_id(output.FloatOutput),
    cv.Optional(CONF_NORMAL_MAX_POWER, default="35%"): cv.percentage,
    cv.Optional(CONF_EXCEPTION_MAX_POWER, default="20%"): cv.percentage,
    cv.Optional(CONF_IDLE_BRIGHTNESS, default="10%"): cv.percentage,
    cv.Optional(CONF_RENDER_INTERVAL, default="20ms"):
        cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    normal = await cg.get_variable(config[CONF_NORMAL_OUTPUT])
    cg.add(var.set_normal_output(normal))
    if CONF_EXCEPTION_OUTPUT in config:
        exception = await cg.get_variable(config[CONF_EXCEPTION_OUTPUT])
        cg.add(var.set_exception_output(exception))
    cg.add(var.set_normal_max_power(config[CONF_NORMAL_MAX_POWER]))
    cg.add(var.set_exception_max_power(config[CONF_EXCEPTION_MAX_POWER]))
    cg.add(var.set_idle_brightness(config[CONF_IDLE_BRIGHTNESS]))
    cg.add(var.set_render_interval(config[CONF_RENDER_INTERVAL]))

