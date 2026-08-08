#pragma once

#include "esphome/core/component.h"

#include <cstdint>

namespace ble_mesh_gateway {

struct MeshNode {
  uint8_t uuid[16];
  uint8_t addr[6];
  uint16_t unicast_addr;
  uint16_t net_idx;
  bool provisioned;
};

class BleMeshGateway : public esphome::Component {
 public:
  void setup() override;
  void loop() override;
  void on_shutdown() override;

  uint8_t node_count() const { return node_count_; }
  const MeshNode *get_node(uint8_t index) const;

 private:
  bool init_ble_controller();
  bool init_ble_mesh();
  void start_provisioning(const uint8_t *uuid, const uint8_t *addr,
                          uint8_t addr_type, uint16_t oob_info);
  void configure_node(uint16_t node_addr, uint16_t net_idx);

  static constexpr uint8_t kMaxNodes = 10;
  MeshNode nodes_[kMaxNodes];
  uint8_t node_count_{0};
  bool initialized_{false};
};

}  // namespace ble_mesh_gateway
