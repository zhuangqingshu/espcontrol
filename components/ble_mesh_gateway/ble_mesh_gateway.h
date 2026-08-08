#pragma once

#include "esphome/core/component.h"

namespace ble_mesh_gateway {

class BleMeshGateway : public esphome::Component {
 public:
  void setup() override;
  void loop() override;
};

}  // namespace ble_mesh_gateway
