#include "ble_mesh_gateway.h"

#include "esphome/core/log.h"

namespace ble_mesh_gateway {

static const char *const TAG = "ble_mesh_gateway";

void BleMeshGateway::setup() {
  ESP_LOGI(TAG, "BLE Mesh Gateway component starting");
}

void BleMeshGateway::loop() {
}

}  // namespace ble_mesh_gateway
