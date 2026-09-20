#include "ble_mesh_switch.h"
#include "ble_mesh_gateway.h"

namespace ble_mesh_gateway {

void BleMeshSwitch::write_state(bool state) {
  if (gateway_ != nullptr) {
    gateway_->send_generic_onoff_set(unicast_addr_, net_idx_, state);
  }
}

}  // namespace ble_mesh_gateway
