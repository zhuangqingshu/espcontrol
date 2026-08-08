#include "ble_mesh_gateway.h"
#include "ble_mesh_switch.h"
#include "ble_mesh_number.h"

#include "esphome/components/number/number.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include "esp_bt.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"

#include <cstdio>
#include <cstring>

namespace ble_mesh_gateway {

static const char *const TAG = "ble_mesh_gateway";

static constexpr uint8_t kDevUuid[16] = {0xDD, 0xDD};
static constexpr uint8_t kNetKey[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
static constexpr uint8_t kAppKey[16] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                                        0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};
static constexpr uint16_t kNetKeyIdx = 0;
static constexpr uint16_t kAppKeyIdx = 0;

static BleMeshGateway *s_instance = nullptr;

static esp_ble_mesh_prov_t provision = {
    .prov_uuid = kDevUuid,
    .prov_unicast_addr = 0x0001,
    .prov_start_address = 0x0002,
    .prov_attention = 0,
    .prov_algorithm = 0,
    .prov_pub_key_oob = 0,
    .flags = 0,
    .iv_index = 0,
};

static esp_ble_mesh_comp_t composition = {
    .cid = 0xFFFF,
    .pid = 0x0001,
    .vid = 0x0001,
    .element_count = 0,
    .elements = nullptr,
};

static void config_client_callback(
    esp_ble_mesh_cfg_client_cb_event_t event,
    esp_ble_mesh_cfg_client_cb_param_t *param) {
  if (!s_instance)
    return;

  switch (event) {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT: {
      uint16_t src = param->params ? param->params->ctx.addr : 0;
      ESP_LOGI(TAG, "Config GET state from 0x%04X err=%d", src,
               param->error_code);

      if (param->error_code == 0 && param->status_cb.comp_data_status.composition_data) {
        auto *buf = param->status_cb.comp_data_status.composition_data;
        s_instance->handle_composition_data(src, buf->data, buf->len);

        auto *node = s_instance->find_node_by_addr(src);
        if (node) {
          if (node->has_onoff_model) {
            s_instance->bind_onoff_model(src, node->net_idx);
          }
          if (node->has_level_model) {
            s_instance->bind_level_model(src, node->net_idx);
          }
        }
      }
      break;
    }

    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT: {
      uint16_t src = param->params ? param->params->ctx.addr : 0;
      ESP_LOGI(TAG, "Config SET state from 0x%04X err=%d", src,
               param->error_code);

      if (param->error_code == 0) {
        auto *node = s_instance->find_node_by_addr(src);
        if (node) {
          uint16_t model_id =
              param->status_cb.model_app_status.model_id;
          uint8_t slot =
              static_cast<uint8_t>(node - s_instance->get_node(0));
          if (model_id == 0x1000 && node->onoff_switch == nullptr) {
            s_instance->create_onoff_switch(slot);
          } else if (model_id == 0x1002 && node->level_number == nullptr) {
            s_instance->create_level_number(slot);
          }
        }
      }
      break;
    }

    case ESP_BLE_MESH_CFG_CLIENT_PUBLISH_EVT:
      break;

    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
      ESP_LOGW(TAG, "Config client timeout");
      break;

    default:
      break;
  }
}

static void generic_client_callback(
    esp_ble_mesh_generic_client_cb_event_t event,
    esp_ble_mesh_generic_client_cb_param_t *param) {
  if (!s_instance)
    return;

  switch (event) {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT: {
      uint16_t src = param->params ? param->params->ctx.addr : 0;
      uint32_t opcode = param->params ? param->params->opcode : 0;

      if (opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS) {
        bool on = param->status_cb.onoff_status.present_onoff != 0;
        ESP_LOGI(TAG, "OnOff status from 0x%04X: %s", src, on ? "ON" : "OFF");

        auto *node = s_instance->find_node_by_addr(src);
        if (node) {
          node->onoff_state = on;
          if (node->onoff_switch) {
            node->onoff_switch->publish_state(on);
          }
        }
      } else if (opcode == ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_STATUS) {
        int16_t level = param->status_cb.level_status.present_level;
        ESP_LOGI(TAG, "Level status from 0x%04X: %d", src, level);

        auto *node = s_instance->find_node_by_addr(src);
        if (node) {
          node->level_state = level;
          if (node->level_number) {
            node->level_number->publish_state(level);
          }
        }
      }
      break;
    }

    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
      ESP_LOGI(TAG, "Generic OnOff set ack");
      break;

    case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
      ESP_LOGI(TAG, "Generic OnOff publish event");
      break;

    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
      ESP_LOGW(TAG, "Generic OnOff timeout");
      break;

    default:
      break;
  }
}

static void provisioner_callback(esp_ble_mesh_prov_cb_event_t event,
                                 esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
      ESP_LOGI(TAG, "Provisioner registered");
      break;

    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT: {
      auto &pkt = param->provisioner_recv_unprov_adv_pkt;
      char uuid_str[37];
      for (int i = 0; i < 16; i++) {
        snprintf(uuid_str + i * 2, 3, "%02X", pkt.dev_uuid[i]);
      }
      ESP_LOGI(TAG, "Discovered device UUID=%s addr=%02X:%02X:%02X:%02X:%02X:%02X rssi=%d",
               uuid_str, pkt.addr[0], pkt.addr[1], pkt.addr[2],
               pkt.addr[3], pkt.addr[4], pkt.addr[5], pkt.rssi);

      if (s_instance && s_instance->node_count() < 10) {
        s_instance->start_provisioning(pkt.dev_uuid, pkt.addr,
                                       pkt.addr_type, pkt.oob_info);
      }
      break;
    }

    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
      ESP_LOGI(TAG, "Add unprov device complete, err=%d",
               param->provisioner_add_unprov_dev_comp.err_code);
      break;

    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
      ESP_LOGI(TAG, "Provisioning link opened");
      break;

    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
      ESP_LOGI(TAG, "Provisioning link closed, reason=%d",
               param->provisioner_prov_link_close.reason);
      break;

    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT: {
      auto &comp = param->provisioner_prov_complete;
      ESP_LOGI(TAG, "Provisioning complete: node_idx=%d unicast=0x%04X elements=%d",
               comp.node_idx, comp.unicast_addr, comp.element_num);
      if (s_instance) {
        s_instance->configure_node(comp.unicast_addr, comp.netkey_idx);
      }
      break;
    }

    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_NET_KEY_COMP_EVT:
      ESP_LOGI(TAG, "Net key added, err=%d",
               param->provisioner_add_net_key_comp.err_code);
      break;

    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
      ESP_LOGI(TAG, "App key added, err=%d",
               param->provisioner_add_app_key_comp.err_code);
      break;

    default:
      break;
  }
}

void BleMeshGateway::send_generic_onoff_set(uint16_t node_addr,
                                             uint16_t net_idx, bool on) {
  esp_ble_mesh_msg_ctx_t ctx = {};
  ctx.net_idx = net_idx;
  ctx.addr = node_addr;
  ctx.app_idx = kAppKeyIdx;
  ctx.send_ttl = 4;

  esp_ble_mesh_client_common_param_t common = {};
  common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET;
  common.ctx = ctx;
  common.msg_timeout = 0;

  esp_ble_mesh_generic_client_set_state_t set_state = {};
  set_state.onoff_set.onoff = on ? 1 : 0;
  set_state.onoff_set.tid = 0;

  esp_err_t ret = esp_ble_mesh_generic_client_set_state(&common,
                                                          &set_state);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to send OnOff set to 0x%04X: %d", node_addr, ret);
  } else {
    ESP_LOGI(TAG, "Sent OnOff=%d to node 0x%04X", on, node_addr);
  }
}

void BleMeshGateway::send_generic_onoff_get(uint16_t node_addr,
                                             uint16_t net_idx) {
  esp_ble_mesh_msg_ctx_t ctx = {};
  ctx.net_idx = net_idx;
  ctx.addr = node_addr;
  ctx.app_idx = kAppKeyIdx;
  ctx.send_ttl = 4;

  esp_ble_mesh_client_common_param_t common = {};
  common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET;
  common.ctx = ctx;
  common.msg_timeout = 0;

  esp_ble_mesh_generic_client_get_state_t get_state = {};
  esp_err_t ret = esp_ble_mesh_generic_client_get_state(&common, &get_state);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to send OnOff get to 0x%04X: %d", node_addr, ret);
  } else {
    ESP_LOGI(TAG, "Sent OnOff get to node 0x%04X", node_addr);
  }
}

void BleMeshGateway::send_generic_level_set(uint16_t node_addr,
                                             uint16_t net_idx,
                                             int16_t level) {
  esp_ble_mesh_msg_ctx_t ctx = {};
  ctx.net_idx = net_idx;
  ctx.addr = node_addr;
  ctx.app_idx = kAppKeyIdx;
  ctx.send_ttl = 4;

  esp_ble_mesh_client_common_param_t common = {};
  common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET;
  common.ctx = ctx;
  common.msg_timeout = 0;

  esp_ble_mesh_generic_client_set_state_t set_state = {};
  set_state.level_set.op_en = false;
  set_state.level_set.level = level;
  set_state.level_set.tid = 0;

  esp_err_t ret = esp_ble_mesh_generic_client_set_state(&common,
                                                          &set_state);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to send Level set to 0x%04X: %d", node_addr, ret);
  } else {
    ESP_LOGI(TAG, "Sent Level=%d to node 0x%04X", level, node_addr);
  }
}

void BleMeshGateway::send_generic_level_get(uint16_t node_addr,
                                             uint16_t net_idx) {
  esp_ble_mesh_msg_ctx_t ctx = {};
  ctx.net_idx = net_idx;
  ctx.addr = node_addr;
  ctx.app_idx = kAppKeyIdx;
  ctx.send_ttl = 4;

  esp_ble_mesh_client_common_param_t common = {};
  common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_GET;
  common.ctx = ctx;
  common.msg_timeout = 0;

  esp_ble_mesh_generic_client_get_state_t get_state = {};
  esp_err_t ret = esp_ble_mesh_generic_client_get_state(&common, &get_state);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to send Level get to 0x%04X: %d", node_addr, ret);
  } else {
    ESP_LOGI(TAG, "Sent Level get to node 0x%04X", node_addr);
  }
}

void BleMeshGateway::start_provisioning(const uint8_t *uuid,
                                         const uint8_t *addr,
                                         uint8_t addr_type,
                                         uint16_t oob_info) {
  esp_ble_mesh_unprov_dev_add_t add_dev = {};
  memcpy(add_dev.addr, addr, 6);
  add_dev.addr_type = static_cast<esp_ble_mesh_addr_type_t>(addr_type);
  memcpy(add_dev.uuid, uuid, 16);
  add_dev.oob_info = oob_info;
  add_dev.bearer = static_cast<esp_ble_mesh_prov_bearer_t>(
      ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);

  esp_err_t ret = esp_ble_mesh_provisioner_add_unprov_dev(
      &add_dev,
      ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_START_PROV_NOW_FLAG |
          ADD_DEV_FLUSHABLE_DEV_FLAG);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to add device for provisioning: %d", ret);
  } else {
    ESP_LOGI(TAG, "Auto-provisioning device");
  }
}

void BleMeshGateway::configure_node(uint16_t node_addr, uint16_t net_idx) {
  if (node_count_ >= kMaxNodes) {
    ESP_LOGW(TAG, "Node table full");
    return;
  }

  auto &node = nodes_[node_count_];
  node.unicast_addr = node_addr;
  node.net_idx = net_idx;
  node.provisioned = true;
  node.onoff_state = false;
  node.level_state = 0;
  node.comp_data_received = false;
  node.has_onoff_model = false;
  node.has_level_model = false;
  node.onoff_switch = nullptr;
  node.level_number = nullptr;
  node_count_++;

  ESP_LOGI(TAG, "Node %d stored: unicast=0x%04X, requesting composition data",
           node_count_ - 1, node_addr);

  request_composition_data(node_addr, net_idx);
}

void BleMeshGateway::request_composition_data(uint16_t node_addr,
                                               uint16_t net_idx) {
  esp_ble_mesh_msg_ctx_t ctx = {};
  ctx.net_idx = net_idx;
  ctx.addr = node_addr;
  ctx.app_idx = kAppKeyIdx;
  ctx.send_ttl = 4;

  esp_ble_mesh_client_common_param_t common = {};
  common.opcode = ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET;
  common.ctx = ctx;
  common.msg_timeout = 4000;

  esp_ble_mesh_cfg_client_get_state_t get_state = {};
  get_state.comp_data_get.page = 0;

  esp_err_t ret =
      esp_ble_mesh_config_client_get_state(&common, &get_state);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to request composition data from 0x%04X: %d",
             node_addr, ret);
  } else {
    ESP_LOGI(TAG, "Requested composition data from 0x%04X", node_addr);
  }
}

void BleMeshGateway::handle_composition_data(uint16_t node_addr,
                                              const uint8_t *data,
                                              uint16_t length) {
  auto *node = find_node_by_addr(node_addr);
  if (!node) {
    ESP_LOGW(TAG, "Composition data from unknown node 0x%04X", node_addr);
    return;
  }

  node->comp_data_received = true;

  ESP_LOGI(TAG, "Composition data from 0x%04X: %d bytes", node_addr, length);

  if (length < 10) {
    ESP_LOGW(TAG, "Composition data too short: %d bytes", length);
    return;
  }

  uint16_t offset = 10;
  uint16_t element_addr = node->unicast_addr;
  bool found_onoff = false;
  bool found_level = false;

  while (offset + 4 <= length) {
    offset += 2;  // location descriptor

    uint8_t num_sig = data[offset++];
    uint8_t num_vendor = data[offset++];

    for (uint8_t i = 0; i < num_sig && offset + 2 <= length; i++) {
      uint16_t model_id = data[offset] | (data[offset + 1] << 8);
      offset += 2;

      if (model_id == 0x1000) {
        ESP_LOGI(TAG, "Found Generic OnOff Server on node 0x%04X element 0x%04X",
                 node->unicast_addr, element_addr);
        found_onoff = true;
      } else if (model_id == 0x1002) {
        ESP_LOGI(TAG, "Found Generic Level Server on node 0x%04X element 0x%04X",
                 node->unicast_addr, element_addr);
        found_level = true;
      }
    }

    offset += num_vendor * 4;
    element_addr++;
  }

  node->has_onoff_model = found_onoff;
  node->has_level_model = found_level;

  if (!found_onoff && !found_level) {
    ESP_LOGI(TAG, "Node 0x%04X has no supported models", node_addr);
  }
}

void BleMeshGateway::bind_onoff_model(uint16_t node_addr, uint16_t net_idx) {
  esp_ble_mesh_msg_ctx_t ctx = {};
  ctx.net_idx = net_idx;
  ctx.addr = node_addr;
  ctx.app_idx = kAppKeyIdx;
  ctx.send_ttl = 4;

  esp_ble_mesh_client_common_param_t common = {};
  common.opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND;
  common.ctx = ctx;
  common.msg_timeout = 4000;

  esp_ble_mesh_cfg_client_set_state_t set_state = {};
  set_state.model_app_bind.element_addr = node_addr;
  set_state.model_app_bind.model_app_idx = kAppKeyIdx;
  set_state.model_app_bind.model_id = 0x1000;
  set_state.model_app_bind.company_id = 0xFFFF;

  esp_err_t ret = esp_ble_mesh_config_client_set_state(&common, &set_state);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to send model app bind: %d", ret);
  } else {
    ESP_LOGI(TAG, "Sent app key bind for Generic OnOff (0x1000) on 0x%04X",
             node_addr);
  }
}

void BleMeshGateway::create_onoff_switch(uint8_t slot) {
  if (slot >= node_count_) {
    ESP_LOGW(TAG, "create_onoff_switch: invalid slot %d", slot);
    return;
  }

  auto &node = nodes_[slot];

  auto *sw = new BleMeshSwitch();
  sw->set_gateway(this);
  sw->set_node_addr(node.unicast_addr);
  sw->set_net_idx(node.net_idx);

  char name[64];
  snprintf(name, sizeof(name), "BLE Mesh 0x%04X", node.unicast_addr);
  uint32_t hash = 0x424C0000 | (node.unicast_addr & 0xFFFF);

  esphome::App.register_switch(sw, name, hash, 0);

  node.onoff_switch = sw;
  ESP_LOGI(TAG, "Created switch entity '%s' for node 0x%04X", name,
           node.unicast_addr);

  send_generic_onoff_get(node.unicast_addr, node.net_idx);
}

void BleMeshGateway::bind_level_model(uint16_t node_addr, uint16_t net_idx) {
  esp_ble_mesh_msg_ctx_t ctx = {};
  ctx.net_idx = net_idx;
  ctx.addr = node_addr;
  ctx.app_idx = kAppKeyIdx;
  ctx.send_ttl = 4;

  esp_ble_mesh_client_common_param_t common = {};
  common.opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND;
  common.ctx = ctx;
  common.msg_timeout = 4000;

  esp_ble_mesh_cfg_client_set_state_t set_state = {};
  set_state.model_app_bind.element_addr = node_addr;
  set_state.model_app_bind.model_app_idx = kAppKeyIdx;
  set_state.model_app_bind.model_id = 0x1002;
  set_state.model_app_bind.company_id = 0xFFFF;

  esp_err_t ret = esp_ble_mesh_config_client_set_state(&common, &set_state);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to send level model app bind: %d", ret);
  } else {
    ESP_LOGI(TAG, "Sent app key bind for Generic Level (0x1002) on 0x%04X",
             node_addr);
  }
}

void BleMeshGateway::create_level_number(uint8_t slot) {
  if (slot >= node_count_) {
    ESP_LOGW(TAG, "create_level_number: invalid slot %d", slot);
    return;
  }

  auto &node = nodes_[slot];

  auto *num = new BleMeshNumber();
  num->set_gateway(this);
  num->set_node_addr(node.unicast_addr);
  num->set_net_idx(node.net_idx);
  num->traits.set_min_value(-32768);
  num->traits.set_max_value(32767);
  num->traits.set_step(1);

  char name[64];
  snprintf(name, sizeof(name), "BLE Mesh 0x%04X Level", node.unicast_addr);
  uint32_t hash = 0x424C0000 | (node.unicast_addr & 0xFFFF) | 0x100;

  esphome::App.register_number(num, name, hash, 0);

  node.level_number = num;
  ESP_LOGI(TAG, "Created number entity '%s' for node 0x%04X", name,
           node.unicast_addr);

  send_generic_level_get(node.unicast_addr, node.net_idx);
}

MeshNode *BleMeshGateway::find_node_by_addr(uint16_t addr) {
  for (uint8_t i = 0; i < node_count_; i++) {
    if (nodes_[i].unicast_addr == addr) {
      return &nodes_[i];
    }
  }
  return nullptr;
}

const MeshNode *BleMeshGateway::get_node(uint8_t index) const {
  if (index < node_count_) {
    return &nodes_[index];
  }
  return nullptr;
}

bool BleMeshGateway::init_ble_controller() {
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_err_t ret;

  ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to release classic BT: %s", esp_err_to_name(ret));
  }

  ret = esp_bt_controller_init(&bt_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
    return false;
  }

  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
    return false;
  }

  ESP_LOGI(TAG, "BLE controller initialized");
  return true;
}

bool BleMeshGateway::init_ble_mesh() {
  esp_err_t ret;

  esp_ble_mesh_register_prov_callback(provisioner_callback);
  esp_ble_mesh_register_config_client_callback(config_client_callback);
  esp_ble_mesh_register_generic_client_callback(generic_client_callback);

  ret = esp_ble_mesh_init(&provision, &composition);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BLE Mesh init failed: %d", ret);
    return false;
  }

  ret = esp_ble_mesh_provisioner_set_dev_uuid_match(
      kDevUuid, sizeof(kDevUuid), 0, false);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Set dev UUID match failed: %d", ret);
  }

  ret = esp_ble_mesh_provisioner_add_local_net_key(kNetKey, kNetKeyIdx);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Add net key failed: %d", ret);
    return false;
  }

  ret = esp_ble_mesh_provisioner_add_local_app_key(
      kAppKey, kNetKeyIdx, kAppKeyIdx);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Add app key failed: %d", ret);
    return false;
  }

  ret = esp_ble_mesh_provisioner_prov_enable(
      static_cast<esp_ble_mesh_prov_bearer_t>(ESP_BLE_MESH_PROV_ADV |
                                              ESP_BLE_MESH_PROV_GATT));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Provisioning enable failed: %d", ret);
    return false;
  }

  ESP_LOGI(TAG, "BLE Mesh provisioner started (net=0x%04X app=0x%04X)",
           kNetKeyIdx, kAppKeyIdx);
  return true;
}

void BleMeshGateway::setup() {
  s_instance = this;
  ESP_LOGI(TAG, "BLE Mesh Gateway component starting");

  if (!init_ble_controller()) {
    ESP_LOGE(TAG, "BLE controller init failed, gateway disabled");
    return;
  }

  if (!init_ble_mesh()) {
    ESP_LOGE(TAG, "BLE Mesh init failed, gateway disabled");
    return;
  }

  initialized_ = true;
  ESP_LOGI(TAG, "BLE Mesh Gateway ready, waiting for unprovisioned devices");
}

void BleMeshGateway::loop() {
}

void BleMeshGateway::on_shutdown() {
  if (initialized_) {
    ESP_LOGI(TAG, "Shutting down BLE Mesh Gateway");
    initialized_ = false;
  }
}

}  // namespace ble_mesh_gateway
