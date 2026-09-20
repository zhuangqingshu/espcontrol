#pragma once

#include "esphome/components/switch/switch.h"

namespace ble_mesh_gateway {

class BleMeshGateway;

class BleMeshSwitch : public esphome::switch_::Switch {
 public:
  void set_gateway(BleMeshGateway *gateway) { gateway_ = gateway; }
  void set_node_addr(uint16_t addr) { unicast_addr_ = addr; }
  void set_net_idx(uint16_t idx) { net_idx_ = idx; }

 protected:
  void write_state(bool state) override;

  BleMeshGateway *gateway_{nullptr};
  uint16_t unicast_addr_{0};
  uint16_t net_idx_{0};
};

}  // namespace ble_mesh_gateway
