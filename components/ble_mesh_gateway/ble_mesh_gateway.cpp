#include "ble_mesh_gateway.h"

#include "esphome/core/log.h"

#include "esp_bt.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"

namespace ble_mesh_gateway {

static const char *const TAG = "ble_mesh_gateway";

static constexpr uint8_t kDeviceUuid[16] = {0xDD, 0xDD};

static esp_ble_mesh_prov_t provision = {};
static esp_ble_mesh_comp_t composition = {};

static void provisioner_callback(esp_ble_mesh_prov_cb_event_t event,
                                 esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT: {
      char uuid_str[37];
      for (int i = 0; i < 16; i++) {
        snprintf(uuid_str + i * 2, 3, "%02X", param->provisioner_recv_unprov_adv_pkt.dev_uuid[i]);
      }
      ESP_LOGI(TAG, "Discovered unprovisioned device UUID=%s addr=%02X:%02X:%02X:%02X:%02X:%02X",
               uuid_str,
               param->provisioner_recv_unprov_adv_pkt.addr[0],
               param->provisioner_recv_unprov_adv_pkt.addr[1],
               param->provisioner_recv_unprov_adv_pkt.addr[2],
               param->provisioner_recv_unprov_adv_pkt.addr[3],
               param->provisioner_recv_unprov_adv_pkt.addr[4],
               param->provisioner_recv_unprov_adv_pkt.addr[5]);
      break;
    }
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
      ESP_LOGI(TAG, "Provisioning link opened");
      break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
      ESP_LOGI(TAG, "Provisioning link closed, reason=%d",
               param->provisioner_prov_link_close.reason);
      break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
      ESP_LOGI(TAG, "Provisioning complete");
      break;
    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
      ESP_LOGI(TAG, "Provisioning started");
      break;
    default:
      break;
  }
}

static void config_client_callback(esp_ble_mesh_cfg_client_cb_event_t event,
                                   esp_ble_mesh_cfg_client_cb_param_t *param) {
  ESP_LOGI(TAG, "Config client event: %d", event);
}

bool BleMeshGateway::init_ble_controller() {
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_err_t ret;

  ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to release classic BT memory: %s", esp_err_to_name(ret));
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

  provision.prov_uuid = kDeviceUuid;
  provision.prov_unicast_addr = 0x0001;
  provision.prov_start_address = 0x0002;
  provision.prov_attention = 0;
  provision.prov_algorithm = 0;
  provision.prov_bearer = 0;
  provision.flags = ESP_BLE_MESH_PROV_FLAGS_PROVISIONER;

  composition.cid = 0xFFFF;
  composition.pid = 0x0001;
  composition.vid = 0x0001;
  composition.crpl = 10;

  ret = ble_mesh_init(&provision, &composition);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BLE Mesh init failed: %d", ret);
    return false;
  }

  esp_ble_mesh_provisioner_set_dev_uuid_match(
      const_cast<uint8_t *>(kDeviceUuid), sizeof(kDeviceUuid), 0, false);

  ret = esp_ble_mesh_provisioner_prov_enable(
      static_cast<esp_ble_mesh_prov_bearer_flag_t>(
          ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Provisioning enable failed: %d", ret);
    return false;
  }

  ESP_LOGI(TAG, "BLE Mesh provisioner started, scanning for devices");
  return true;
}

void BleMeshGateway::setup() {
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
