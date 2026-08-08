#pragma once

#include "esphome/components/switch/switch.h"

namespace ble_mesh_gateway {

class BleMeshGateway;

class BleMeshSwitch : public esphome::switch_::Switch {
 public:
  void set_gateway(BleMeshGateway *gateway) { gateway_ = gateway; }
  void set_slot(uint8_t slot) { slot_ = slot; }

 protected:
  void write_state(bool state) override;

  BleMeshGateway *gateway_{nullptr};
  uint8_t slot_{0};
};

}  // namespace ble_mesh_gateway
