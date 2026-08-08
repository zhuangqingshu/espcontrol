#pragma once

#include "esphome/core/component.h"

#include <cstdint>

namespace esphome {
namespace switch_ {
class Switch;
}  // namespace switch_
}  // namespace esphome

namespace ble_mesh_gateway {

struct MeshNode {
  uint8_t uuid[16];
  uint8_t addr[6];
  uint16_t unicast_addr;
  uint16_t net_idx;
  bool provisioned;
  bool onoff_state;
};

class BleMeshGateway : public esphome::Component {
 public:
  void setup() override;
  void loop() override;
  void on_shutdown() override;

  uint8_t node_count() const { return node_count_; }
  const MeshNode *get_node(uint8_t index) const;
  void start_provisioning(const uint8_t *uuid, const uint8_t *addr,
                          uint8_t addr_type, uint16_t oob_info);
  void configure_node(uint16_t node_addr, uint16_t net_idx);

  void set_node_onoff(uint8_t slot, bool state);
  bool get_node_onoff(uint8_t slot) const;
  void register_switch(uint8_t slot, esphome::switch_::Switch *sw);

 private:
  bool init_ble_controller();
  bool init_ble_mesh();
  void send_generic_onoff_set(uint16_t node_addr, uint16_t net_idx, bool on);
  void update_switch_state(uint8_t slot, bool on);

  static constexpr uint8_t kMaxNodes = 10;
  MeshNode nodes_[kMaxNodes];
  uint8_t node_count_{0};
  bool initialized_{false};
  esphome::switch_::Switch *switches_[kMaxNodes] = {};
};

}  // namespace ble_mesh_gateway
