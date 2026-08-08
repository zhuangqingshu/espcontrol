"""BLE Mesh Gateway component for ESPHome.

Registers a BLE Mesh provisioner and gateway that discovers, provisions,
and controls BLE Mesh devices. Mesh device models are mapped to ESPHome
entities (switch, light, sensor, etc.) so the existing espcontrol UI
layer can display and control them without modification.
"""
from __future__ import annotations

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@jtenniswood"]

ble_mesh_gateway_ns = cg.esphome_ns.namespace("ble_mesh_gateway")
BleMeshGateway = ble_mesh_gateway_ns.class_("BleMeshGateway", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(BleMeshGateway),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
