#pragma once

#include "esphome/core/component.h"

#include <cstdint>

namespace esphome {
namespace switch_ {
class Switch;
}
namespace number {
class Number;
}
}  // namespace esphome

namespace ble_mesh_gateway {

struct MeshNode {
  uint8_t uuid[16];
  uint8_t addr[6];
  uint16_t unicast_addr;
  uint16_t net_idx;
  bool provisioned;
  bool onoff_state;
  int16_t level_state;
  bool comp_data_received;
  bool has_onoff_model;
  bool has_level_model;
  esphome::switch_::Switch *onoff_switch;
  esphome::number::Number *level_number;
};

class BleMeshGateway : public esphome::Component {
 public:
  void setup() override;
  void loop() override;
  void on_shutdown() override;

  uint8_t node_count() const { return node_count_; }
  const MeshNode *get_node(uint8_t index) const;
  MeshNode *find_node_by_addr(uint16_t addr);
  void start_provisioning(const uint8_t *uuid, const uint8_t *addr,
                          uint8_t addr_type, uint16_t oob_info);
  void configure_node(uint16_t node_addr, uint16_t net_idx);

  void send_generic_onoff_set(uint16_t node_addr, uint16_t net_idx, bool on);
  void send_generic_onoff_get(uint16_t node_addr, uint16_t net_idx);
  void send_generic_level_set(uint16_t node_addr, uint16_t net_idx,
                              int16_t level);
  void send_generic_level_get(uint16_t node_addr, uint16_t net_idx);

  void request_composition_data(uint16_t node_addr, uint16_t net_idx);
  void handle_composition_data(uint16_t node_addr, const uint8_t *data,
                                uint16_t length);
  void bind_onoff_model(uint16_t node_addr, uint16_t net_idx);
  void bind_level_model(uint16_t node_addr, uint16_t net_idx);
  void create_onoff_switch(uint8_t slot);
  void create_level_number(uint8_t slot);

 private:
  bool init_ble_controller();
  bool init_ble_mesh();

  static constexpr uint8_t kMaxNodes = 10;
  MeshNode nodes_[kMaxNodes];
  uint8_t node_count_{0};
  bool initialized_{false};
};

}  // namespace ble_mesh_gateway
