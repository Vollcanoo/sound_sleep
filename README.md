# ESP32 智能防鼾睡姿调节系统 — 云端 LLM 分析模块

基于 ESP32-S3 + FreeRTOS + 火山引擎大模型 API 的智能防鼾系统。整合鼾声检测、睡姿识别和气囊控制，通过云端 LLM 分析生成个性化调节指令。

## 系统全流程

```
┌─────────────────────┐    ┌──────────────────────┐
│  Snore_Det_esp      │    │  Posture_Recognition │
│  INMP441 麦克风     │    │  FSR×3 压力传感器    │
│  GPIO14/15/32 (I2S) │    │  GPIO4/5/6 (ADC)     │
│  → 鼾声概率 0~1     │    │  → 睡姿分类+置信度  │
└────────┬────────────┘    └────────┬─────────────┘
         │                          │
         └──────────┬───────────────┘
                    ▼
         ┌──────────────────────┐
         │  sleep_llm (本模块)  │
         │  FreeRTOS Queue      │
         │  → 云端 LLM 分析     │
         │  → 气泵控制指令      │
         └──────────┬───────────┘
                    ▼
         ┌──────────────────────┐
         │  airbag-hardware     │
         │  左气泵 GPIO7        │
         │  右气泵 GPIO8        │
         │  左阀门 GPIO9        │
         │  右阀门 GPIO10       │
         │  → 枕头高度调节      │
         └──────────────────────┘
```

## GPIO 分配总览

| GPIO | 用途 | 所属分支 |
|------|------|---------|
| 4, 5, 6 | FSR 压力传感器 (ADC) | Posture_Recognition |
| 7, 8 | 左/右气泵 (MOS驱动) | airbag-hardware |
| 9, 10 | 左/右电磁阀 (AO3400A) | airbag-hardware |
| 14, 15, 32 | INMP441 I2S 麦克风 | Snore_Det_esp |

## 数据合约

### 鼾声数据 (对齐 `snore_model_output.template.json`)
- 顶层参数：`window_seconds=5.0`, `hop_seconds=5.0`, `decision_threshold=0.44`
- Summary 9 字段：`window_count`, `mean_probability`, `max_probability`, `positive_window_count`, `positive_window_ratio`, `positive_duration_seconds/minutes`, `snore_detected`, `snore_minutes_per_hour`

### 睡姿数据 (对齐 Posture_Recognition)
- `posture`: 枚举 (SUPINE/LEFT_SIDE/RIGHT_SIDE/PRONE/MOVING/NO_HEAD/UNCERTAIN)
- `confidence`: 0.0~1.0
- `x_center_cm`: 头部左右偏移 (负=偏左, 正=偏右)

## 文件说明

| 文件 | 职责 |
|------|------|
| `main.c` | FreeRTOS 任务调度，4 场景 mock 测试 |
| `snore_feature.h` | 数据结构 (鼾声+睡姿，对齐各分支) |
| `cloud_llm_client.h/c` | 火山引擎 LLM API 客户端 |
| `wifi_manager.h/c` | Wi-Fi STA 连接管理 |
| `pump_controller.h/c` | 双气囊 GPIO 控制 (对齐 airbag-hardware) |

## 配置

1. **Wi-Fi**（`wifi_manager.h`）：
   ```c
   #define WIFI_SSID     "你的WiFi名称"    // 仅支持 2.4GHz
   #define WIFI_PASSWORD "你的WiFi密码"
   ```

2. **API Key**（`cloud_llm_client.h`）：
   ```c
   #define VOLCENGINE_API_KEY  "your-api-key-here"
   ```

## 编译与烧录

```bash
. $IDF_PATH/export.sh
idf.py build
idf.py -p COMx flash monitor
```

## LLM 决策规则

| 鼾声 | 睡姿 | 动作 |
|------|------|------|
| 未检测到 | 任意 | hold |
| 严重 (≥4分/时) | 仰卧 | inflate right (促使左侧卧) |
| 中等 (2-4分/时) | 仰卧 | inflate right, 低强度 |
| 轻微 (<2分/时) | 任意 | hold |
| 严重 | 左侧卧 | inflate left (促使转向) |
| 严重 | 右侧卧 | inflate right |
| 任意 | 翻身中 | hold (等待稳定) |
| 任意 | 头不在枕 | hold |

## 同学集成方式

```c
#include "snore_feature.h"
extern QueueHandle_t g_feature_queue;

snore_features_t feat = {
    // 鼾声 (从 Snore_Det_esp 累积)
    .window_seconds = 5.0, .hop_seconds = 5.0,
    .decision_threshold = 0.44,
    .window_count = ..., .mean_probability = ...,
    .max_probability = ..., .snore_detected = ...,
    .snore_minutes_per_hour = ...,
    // 睡姿 (从 Posture_Recognition 获取)
    .posture = {
        .posture = POSTURE_SUPINE,
        .confidence = 0.85,
        .x_center_cm = 0.12,
        .y_center_cm = 0.31,
    },
};
xQueueSend(g_feature_queue, &feat, portMAX_DELAY);
```

## API 测试

```bash
pip install requests
python test_llm_api.py
```

## 依赖

- ESP-IDF v5.5.2
- ESP32-S3 开发板
- 火山引擎边缘智能 API
