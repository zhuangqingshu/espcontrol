"""BLE Mesh Gateway component for ESPHome.

Registers a BLE Mesh provisioner and gateway that discovers, provisions,
and controls BLE Mesh devices. Mesh device models are mapped to ESPHome
entities (switch, light, sensor, etc.) so the existing espcontrol UI
layer can display and control them without modification.
"""
from __future__ import annotations

import esphome.codegen as cg
from esphome.components.esp32 import add_idf_sdkconfig_option
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

    add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_MESH", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_MESH_PROVISIONER", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_MESH_PROXY", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_MESH_RELAY", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ROLE_CENTRAL", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ROLE_OBSERVER", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_MAX_CONNECTIONS", 4)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT", 12)
    add_idf_sdkconfig_option("CONFIG_BT_CTRL_BLE_MAX_ACT", 10)
