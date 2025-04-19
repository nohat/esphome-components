# components/wiz_client/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Configuration keys
CONF_BULBS = 'bulbs'
CONF_INITIAL_BRIGHTNESS = 'initial_brightness'
CONF_INITIAL_COLOR_TEMPERATURE = 'initial_temp'

# Create a namespace matching your C++ namespace (wiz_client)
wiz_client_ns = cg.esphome_ns.namespace('wiz_client')
# Declare the C++ class
WizClient = wiz_client_ns.class_('WizClient', cg.Component)

# Define the schema for the wiz_client: YAML block
definition = cv.Schema({
    cv.GenerateID(): cv.declare_id(WizClient),
    cv.Required(CONF_BULBS): cv.All(
        cv.ensure_list(cv.string),
        cv.Length(min=1)
    ),
    cv.Optional(CONF_INITIAL_BRIGHTNESS, default=100): cv.int_range(0, 100),
    cv.Optional(CONF_INITIAL_COLOR_TEMPERATURE, default=2700): cv.int_range(1700, 6500),
})

# Extend the base component schema so ESPHome can recognize
CONFIG_SCHEMA = definition.extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    # Instantiate the C++ object
    var = cg.new_Pvariable(config[CONF_ID])
    # Register with the component system (calls setup())
    await cg.register_component(var, config)

    # Add each bulb IP
    for ip_str in config[CONF_BULBS]:
        cg.add(var.add_bulb(ip_str))

    # Set initial brightness and color temperature
    cg.add(var.set_brightness(config[CONF_INITIAL_BRIGHTNESS]))
    cg.add(var.set_color_temperature(config[CONF_INITIAL_COLOR_TEMPERATURE]))
