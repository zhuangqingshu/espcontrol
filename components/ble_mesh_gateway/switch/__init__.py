"""BLE Mesh Switch platform for ESPHome.

Each switch maps to a Generic OnOff model on a provisioned BLE Mesh node.
"""
from __future__ import annotations

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID

from .. import ble_mesh_gateway_ns

DEPENDENCIES = ["ble_mesh_gateway"]

CONF_SLOT = "slot"

BleMeshSwitch = ble_mesh_gateway_ns.class_("BleMeshSwitch", switch.Switch)

CONFIG_SCHEMA = switch.switch_schema(BleMeshSwitch).extend(
    {
        cv.GenerateID(): cv.declare_id(BleMeshSwitch),
        cv.Required(CONF_SLOT): cv.uint8_t,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await switch.register_switch(var, config)
    cg.add(var.set_slot(config[CONF_SLOT]))
    gateway = await cg.get_variable("ble_mesh_gw")
    cg.add(var.set_gateway(gateway))
