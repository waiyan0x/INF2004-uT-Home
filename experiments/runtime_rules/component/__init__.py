"""ESPHome-only C++ boundary around the shared C transaction model."""
import esphome.codegen as cg
import esphome.config_validation as cv
from pathlib import Path
from esphome.components import i2c, esp32
from esphome.const import CONF_ID

DEPENDENCIES = ["mqtt", "i2c"]
space = cg.esphome_ns.namespace("runtime_rules")
Bridge = space.class_("RuleBridge", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Bridge),
    cv.Required("i2c_id"): cv.use_id(i2c.I2CBus),
    cv.Optional("topic_prefix", default="uthome/rule-lab"): cv.string_strict,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    esp32.add_extra_build_file("runtime_rules_policy.cmake",
                              Path(__file__).with_name("compile_policy.cmake"))
    cg.add_cmake_arg("CMAKE_PROJECT_uthome_rule_lab_INCLUDE", "runtime_rules_policy.cmake")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    bus = await cg.get_variable(config["i2c_id"])
    cg.add(var.set_bus(bus))
    cg.add(var.set_topic_prefix(config["topic_prefix"]))
