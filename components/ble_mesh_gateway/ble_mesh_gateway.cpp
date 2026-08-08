#include "ble_mesh_gateway.h"

#include "esphome/core/log.h"

#include "esp_bt.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"

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

    case ESP_BLE_MESH_PROVISIONER_ADD_NET_KEY_COMP_EVT:
      ESP_LOGI(TAG, "Net key added, err=%d",
               param->provisioner_add_net_key_comp.err_code);
      break;

    case ESP_BLE_MESH_PROVISIONER_ADD_APP_KEY_COMP_EVT:
      ESP_LOGI(TAG, "App key added, err=%d",
               param->provisioner_add_app_key_comp.err_code);
      break;

    default:
      break;
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
  node_count_++;

  ESP_LOGI(TAG, "Node %d stored: unicast=0x%04X", node_count_ - 1, node_addr);

  esp_ble_mesh_msg_ctx_t ctx = {};
  ctx.net_idx = net_idx;
  ctx.addr = node_addr;
  ctx.app_idx = kAppKeyIdx;
  ctx.send_ttl = 4;
  ctx.send_rel = false;

  esp_ble_mesh_client_common_param_t common = {};
  common.opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND;
  common.ctx = ctx;
  common.msg_timeout = 0;

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
