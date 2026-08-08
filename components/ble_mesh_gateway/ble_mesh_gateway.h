#pragma once

#include "esphome/core/component.h"

namespace ble_mesh_gateway {

class BleMeshGateway : public esphome::Component {
 public:
  void setup() override;
  void loop() override;
  void on_shutdown() override;

 private:
  bool init_ble_controller();
  bool init_ble_mesh();
  static void mesh_event_handler(void *arg, const char *event_type,
                                 const void *event_data, size_t event_len);

  bool initialized_{false};
};

}  // namespace ble_mesh_gateway
