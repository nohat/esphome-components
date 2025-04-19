import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import component

# Create a namespace for the Wiz component.
wiz_ns = cg.esphome_ns.namespace('wiz_client')
WizClient = wiz_ns.class_('WizClient', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.Optional('name'): cv.string,
}).extend(component.COMPONENT_SCHEMA.schema)

def to_code(config):
    var = cg.new_Pvariable(config.get('name', 'wiz_client'), WizClient)
    cg.add(var.setup())