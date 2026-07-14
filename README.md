# ESP32 智能防鼾睡姿调节系统 — 云端 LLM 模块

基于 ESP32-S3 + FreeRTOS + 火山引擎大模型 API 的智能防鼾系统。接收鼾声检测模型输出，通过云端 LLM 分析并生成气泵控制指令，自动调节睡姿。

## 系统架构

```
鼾声检测模型 → FreeRTOS Queue → 云端LLM分析 → 气泵控制
(同学负责)       snore_features_t    (本模块)      pump_controller
```

## 数据合约

严格对齐 `snore_model_output.template.json`：
- 顶层参数：`window_seconds`, `hop_seconds`, `decision_threshold`
- Summary 9 字段：`window_count`, `mean_probability`, `max_probability`, `positive_window_count`, `positive_window_ratio`, `positive_duration_seconds`, `positive_duration_minutes`, `snore_detected`, `snore_minutes_per_hour`

## 文件说明

| 文件 | 职责 |
|------|------|
| `main.c` | FreeRTOS 任务调度，mock 测试数据 |
| `snore_feature.h` | 数据结构定义（对齐 JSON 模板） |
| `cloud_llm_client.h/c` | 火山引擎 LLM API 客户端 |
| `wifi_manager.h/c` | Wi-Fi STA 连接管理 |
| `pump_controller.h/c` | 气泵 GPIO 控制 |

## 配置

使用前需修改以下配置：

1. **Wi-Fi**（`wifi_manager.h`）：
   ```c
   #define WIFI_SSID     "你的WiFi名称"    // 仅支持 2.4GHz
   #define WIFI_PASSWORD "你的WiFi密码"
   ```

2. **API Key**（`cloud_llm_client.h`）：
   ```c
   #define VOLCENGINE_API_KEY  "your-api-key-here"
   #define VOLCENGINE_MODEL    "deepseek-v4-pro-260425"
   ```

## 编译与烧录

```bash
# 设置 ESP-IDF 环境
. $IDF_PATH/export.sh

# 编译
idf.py build

# 烧录 + 监控串口
idf.py -p COMx flash monitor
```

## 同学集成方式

```c
#include "snore_feature.h"
extern QueueHandle_t g_feature_queue;

snore_features_t feat = {
    .window_seconds         = 5.0,
    .hop_seconds            = 5.0,
    .decision_threshold     = 0.46,
    .window_count           = summary.window_count,
    .mean_probability       = summary.mean_probability,
    .max_probability        = summary.max_probability,
    .positive_window_count  = summary.positive_window_count,
    .positive_window_ratio  = summary.positive_window_ratio,
    .positive_duration_seconds = summary.positive_duration_seconds,
    .positive_duration_minutes = summary.positive_duration_minutes,
    .snore_detected         = summary.snore_detected,
    .snore_minutes_per_hour = summary.snore_minutes_per_hour,
};
xQueueSend(g_feature_queue, &feat, portMAX_DELAY);
```

## LLM 决策规则

| 条件 | 动作 |
|------|------|
| `snore_detected = false` | hold（保持） |
| `snore_minutes_per_hour >= 4` | inflate shoulder（促使侧卧） |
| `snore_minutes_per_hour 2~4` | inflate shoulder, 低强度 30-50 |
| `snore_minutes_per_hour < 2` | hold（轻微暂不干预） |
| `max_probability > 0.9 且 ratio > 0.1` | inflate head, 强度 70-80 |

## 测试

项目根目录下 `test_llm_api.py` 可脱离 ESP32 单独测试 API 连通性：

```bash
pip install requests
python test_llm_api.py
```

## 依赖

- ESP-IDF v5.5.2
- ESP32-S3 开发板
- 火山引擎边缘智能 API
