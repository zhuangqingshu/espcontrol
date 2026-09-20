# BLE Mesh Gateway

## 目标

为 espcontrol 增加 BLE Mesh Gateway 能力，将 BLE Mesh 网络中的设备接入 Home Assistant，并通过 espcontrol 的可配置 UI 进行控制和展示。

## 硬件

- **首选开发板**：4" 4848S040（ESP32-S3，原生 WiFi + BLE 5.0）
- ESP32-S3 的 ESP-IDF 框架原生支持 `esp_ble_mesh` API（NimBLE 协议栈）
- 无需额外硬件

## 技术路线

新增独立 C++ 组件 `ble_mesh_gateway`，通过 ESPHome 实体系统与现有 UI 对接。不修改 espcontrol UI 层。

## 实体映射

| BLE Mesh Model | ESPHome 实体 |
|---|---|
| Generic OnOff Server (0x1000) | `switch` |
| Light Lightness Server (0x1300) | `light` |
| Light CTL Server (0x1303) | `light` + `number` (色温) |
| Sensor Server (0x1100) | `sensor` |
| Generic Level Server (0x1002) | `number` |

## 数据流

```
触摸按钮 → ESPHome API → ble_mesh_gateway 组件 → ESP BLE Mesh → 物理设备
```

BLE Mesh 设备对 UI 层透明，现有 Switch/Sensor/Light 卡片直接可用。

## 组件结构

```
components/ble_mesh_gateway/
├── __init__.py              # ESPHome 组件注册
├── ble_mesh_gateway.h       # Component 主类
├── ble_mesh_node.h          # 单个 Mesh 节点的状态模型
├── ble_mesh_gateway.cpp     # 实现
```

## 内存评估

- NimBLE 协议栈：~60-80KB RAM
- Mesh 节点模型 + 消息队列 + 配网状态机：~20-40KB
- 合计约 100-120KB 额外 RAM
- S3 Octal PSRAM 约 8MB，UI 占用约 2-3MB，空间充足
- 需启用 `CONFIG_BT_NIMBLE_MESH` (menuconfig)

## 分阶段执行

### 阶段一：组件脚手架 + BLE 扫描
- 创建 `components/ble_mesh_gateway/`，空 Component 编译通过
- 实现 BLE 扫描：发现附近的未配网 Mesh 设备，打印到日志
- 验证：`esphome logs` 能看到 BLE Mesh beacon

### 阶段二：配网流程
- 实现 `esp_ble_mesh_provisioner` 配网
- 设备加入网络后注册为 binary_sensor（在线/离线）

### 阶段三：全功能控制
- Generic OnOff / Light Lightness / Sensor 等 Model 映射为 ESPHome 实体
- 通过 espcontrol UI 直接控制 Mesh 设备

## 不涉及的改动

- 不碰 `button_grid.h` 及 UI C++ 文件
- 不碰 `espcontrol_app.h` / `EspControlAppCore`
- 不碰 `src/webserver/` 网页配置器
- 不碰 `common/config/card_contract.json` 及其他产品源文件
