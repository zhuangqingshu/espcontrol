#include "ble_mesh_number.h"
#include "ble_mesh_gateway.h"

namespace ble_mesh_gateway {

void BleMeshNumber::control(float value) {
  if (gateway_ != nullptr) {
    gateway_->send_generic_level_set(unicast_addr_, net_idx_,
                                     static_cast<int16_t>(value));
  }
}

}  // namespace ble_mesh_gateway
