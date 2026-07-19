/*
  ESP32-S3 + three FSR film sensors for pillow posture estimation.

  This version follows practical lessons from pressure-sensor sleep-posture
  papers:
  1. Do not classify from one instant sample. Use a short window median.
  2. Reject unstable/motion windows before classifying posture.
  3. Use normalized pressure distribution instead of absolute ADC values.
  4. With only three FSRs, classify no head, motion, left side, right side,
     or supine. Prone is intentionally excluded.

  Recommended placement on a 15 cm x 15 cm pillow:

      Back / head side
          [CENTER]

    [LEFT]       [RIGHT]
      Front / shoulder side

  Approximate left-to-right positions:
  - LEFT:   x = -5 cm
  - CENTER: x =  0 cm
  - RIGHT:  x = +5 cm
*/

#include <math.h>

#define FORCE_SENSOR_LEFT_PIN 4
#define FORCE_SENSOR_CENTER_PIN 5
#define FORCE_SENSOR_RIGHT_PIN 6

// Per channel: 3.3V -> FSR -> ADC pin -> 2k ohm -> GND.
// This makes the ADC reading rise as pressure lowers the FSR resistance.
const bool PRESSURE_INCREASES_WITH_FORCE = true;
const bool MIRROR_LEFT_RIGHT = false;

const int ADC_NEAR_GROUND = 5;
const int ADC_NEAR_3V3 = 4090;

const unsigned long SAMPLE_INTERVAL_MS = 100;   // 10 Hz raw sampling
const unsigned long OUTPUT_INTERVAL_MS = 1000;  // 1 Hz posture output

const int CALIBRATION_SAMPLES = 100;
const int WINDOW_SIZE = 31;  // About 3 seconds at 10 Hz

const float SENSOR_X_LEFT_CM = -5.0f;
const float SENSOR_X_CENTER_CM = 0.0f;
const float SENSOR_X_RIGHT_CM = 5.0f;

const float NO_HEAD_TOTAL_THRESHOLD = 180.0f;
const float MOVEMENT_RANGE_RATIO_THRESHOLD = 0.35f;
const float MOVEMENT_TOTAL_RANGE_THRESHOLD = 220.0f;

const float SIDE_X_THRESHOLD_CM = 1.35f;
const float SIDE_DOMINANT_RATIO = 0.42f;

enum Posture {
  POSTURE_NO_HEAD,
  POSTURE_MOVING,
  POSTURE_LEFT_SIDE,
  POSTURE_RIGHT_SIDE,
  POSTURE_SUPINE
};

struct SensorRaw {
  int left;
  int center;
  int right;
};

struct WindowStats {
  float left;
  float center;
  float right;
  float leftRange;
  float centerRange;
  float rightRange;
};

struct Features {
  float total;
  float leftRatio;
  float centerRatio;
  float rightRatio;
  float xCenterCm;
  bool moving;
};

float baselineLeft = 0.0f;
float baselineCenter = 0.0f;
float baselineRight = 0.0f;

float pressureLeftWindow[WINDOW_SIZE];
float pressureCenterWindow[WINDOW_SIZE];
float pressureRightWindow[WINDOW_SIZE];
int windowIndex = 0;
int windowCount = 0;

unsigned long lastOutputMs = 0;

float absFloat(float value) {
  return value < 0.0f ? -value : value;
}

float maxFloat(float a, float b) {
  return a > b ? a : b;
}

float minFloat(float a, float b) {
  return a < b ? a : b;
}

float clampFloat(float value, float minValue, float maxValue) {
  return maxFloat(minValue, minFloat(value, maxValue));
}

float ratio(float value, float total) {
  if (total <= 0.0f) {
    return 0.0f;
  }
  return value / total;
}

float pressureFromRaw(int raw, float baseline) {
  const float delta = PRESSURE_INCREASES_WITH_FORCE ? raw - baseline : baseline - raw;
  return maxFloat(0.0f, delta);
}

void sortFloatArray(float values[], int count) {
  for (int i = 1; i < count; i++) {
    const float key = values[i];
    int j = i - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      j--;
    }
    values[j + 1] = key;
  }
}

float medianOfWindow(const float values[], int count) {
  float copy[WINDOW_SIZE];
  for (int i = 0; i < count; i++) {
    copy[i] = values[i];
  }
  sortFloatArray(copy, count);
  return copy[count / 2];
}

float rangeOfWindow(const float values[], int count) {
  float minValue = values[0];
  float maxValue = values[0];
  for (int i = 1; i < count; i++) {
    minValue = minFloat(minValue, values[i]);
    maxValue = maxFloat(maxValue, values[i]);
  }
  return maxValue - minValue;
}

const char* postureName(Posture posture) {
  switch (posture) {
    case POSTURE_NO_HEAD:
      return "NO_HEAD";
    case POSTURE_MOVING:
      return "MOVING";
    case POSTURE_LEFT_SIDE:
      return "LEFT_SIDE";
    case POSTURE_RIGHT_SIDE:
      return "RIGHT_SIDE";
    case POSTURE_SUPINE:
    default:
      return "SUPINE";
  }
}

void calibrateBaseline() {
  long sumLeft = 0;
  long sumCenter = 0;
  long sumRight = 0;

  Serial.println("Calibrating baseline. Keep pillow unloaded.");

  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    sumLeft += analogRead(FORCE_SENSOR_LEFT_PIN);
    sumCenter += analogRead(FORCE_SENSOR_CENTER_PIN);
    sumRight += analogRead(FORCE_SENSOR_RIGHT_PIN);
    delay(20);
  }

  baselineLeft = (float)sumLeft / CALIBRATION_SAMPLES;
  baselineCenter = (float)sumCenter / CALIBRATION_SAMPLES;
  baselineRight = (float)sumRight / CALIBRATION_SAMPLES;

  windowIndex = 0;
  windowCount = 0;

  Serial.print("Baseline raw L,C,R=");
  Serial.print(baselineLeft, 1);
  Serial.print(',');
  Serial.print(baselineCenter, 1);
  Serial.print(',');
  Serial.println(baselineRight, 1);

  if (
    baselineLeft >= ADC_NEAR_3V3 ||
    baselineCenter >= ADC_NEAR_3V3 ||
    baselineRight >= ADC_NEAR_3V3
  ) {
    Serial.println("WARNING: ADC near 4095. Check that the FSR signal node is not tied to 3.3V.");
  }

  if (
    baselineLeft <= ADC_NEAR_GROUND &&
    baselineCenter <= ADC_NEAR_GROUND &&
    baselineRight <= ADC_NEAR_GROUND
  ) {
    Serial.println("INFO: All channels are near 0 while unloaded. Press each FSR to confirm its raw value rises.");
  }
}

SensorRaw readRawSensors() {
  SensorRaw raw;
  raw.left = analogRead(FORCE_SENSOR_LEFT_PIN);
  raw.center = analogRead(FORCE_SENSOR_CENTER_PIN);
  raw.right = analogRead(FORCE_SENSOR_RIGHT_PIN);

  return raw;
}

void addPressureSample(const SensorRaw& raw) {
  float left = pressureFromRaw(raw.left, baselineLeft);
  float center = pressureFromRaw(raw.center, baselineCenter);
  float right = pressureFromRaw(raw.right, baselineRight);

  if (MIRROR_LEFT_RIGHT) {
    const float temp = left;
    left = right;
    right = temp;
  }

  pressureLeftWindow[windowIndex] = left;
  pressureCenterWindow[windowIndex] = center;
  pressureRightWindow[windowIndex] = right;

  windowIndex = (windowIndex + 1) % WINDOW_SIZE;
  if (windowCount < WINDOW_SIZE) {
    windowCount++;
  }
}

WindowStats calculateWindowStats() {
  WindowStats stats;
  stats.left = medianOfWindow(pressureLeftWindow, windowCount);
  stats.center = medianOfWindow(pressureCenterWindow, windowCount);
  stats.right = medianOfWindow(pressureRightWindow, windowCount);
  stats.leftRange = rangeOfWindow(pressureLeftWindow, windowCount);
  stats.centerRange = rangeOfWindow(pressureCenterWindow, windowCount);
  stats.rightRange = rangeOfWindow(pressureRightWindow, windowCount);
  return stats;
}

Features calculateFeatures(const WindowStats& stats) {
  Features features;
  features.total = stats.left + stats.center + stats.right;
  features.leftRatio = ratio(stats.left, features.total);
  features.centerRatio = ratio(stats.center, features.total);
  features.rightRatio = ratio(stats.right, features.total);

  features.xCenterCm = (
    stats.left * SENSOR_X_LEFT_CM +
    stats.center * SENSOR_X_CENTER_CM +
    stats.right * SENSOR_X_RIGHT_CM
  ) / maxFloat(features.total, 1.0f);

  const float maxRange = maxFloat(stats.leftRange, maxFloat(stats.centerRange, stats.rightRange));
  features.moving = (
    maxRange > MOVEMENT_TOTAL_RANGE_THRESHOLD &&
    ratio(maxRange, maxFloat(features.total, 1.0f)) > MOVEMENT_RANGE_RATIO_THRESHOLD
  );

  return features;
}

Posture classifyPosture(const Features& features, float* confidence) {
  *confidence = 0.0f;

  if (features.total < NO_HEAD_TOTAL_THRESHOLD) {
    *confidence = 0.95f;
    return POSTURE_NO_HEAD;
  }

  if (features.moving) {
    *confidence = 0.80f;
    return POSTURE_MOVING;
  }

  if (
    features.xCenterCm < -SIDE_X_THRESHOLD_CM &&
    features.leftRatio > SIDE_DOMINANT_RATIO
  ) {
    *confidence = clampFloat(0.55f + absFloat(features.xCenterCm) / 5.0f, 0.0f, 0.95f);
    return POSTURE_LEFT_SIDE;
  }

  if (
    features.xCenterCm > SIDE_X_THRESHOLD_CM &&
    features.rightRatio > SIDE_DOMINANT_RATIO
  ) {
    *confidence = clampFloat(0.55f + absFloat(features.xCenterCm) / 5.0f, 0.0f, 0.95f);
    return POSTURE_RIGHT_SIDE;
  }

  // Any stable head-pressure distribution that is not lateral is treated as supine.
  *confidence = clampFloat(0.55f + features.centerRatio * 0.35f, 0.0f, 0.90f);
  return POSTURE_SUPINE;
}

void printOutput(const SensorRaw& raw, const WindowStats& stats, const Features& features) {
  float confidence = 0.0f;
  const Posture posture = classifyPosture(features, &confidence);

  Serial.print(raw.left);
  Serial.print(',');
  Serial.print(raw.center);
  Serial.print(',');
  Serial.print(raw.right);
  Serial.print(',');
  Serial.print(stats.left, 1);
  Serial.print(',');
  Serial.print(stats.center, 1);
  Serial.print(',');
  Serial.print(stats.right, 1);
  Serial.print(',');
  Serial.print(features.total, 1);
  Serial.print(',');
  Serial.print(features.leftRatio, 3);
  Serial.print(',');
  Serial.print(features.centerRatio, 3);
  Serial.print(',');
  Serial.print(features.rightRatio, 3);
  Serial.print(',');
  Serial.print(features.xCenterCm, 2);
  Serial.print(',');
  Serial.print(features.moving ? 1 : 0);
  Serial.print(',');
  Serial.print(postureName(posture));
  Serial.print(',');
  Serial.println(confidence, 2);
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  const char command = Serial.read();
  if (command == 'b' || command == 'B') {
    calibrateBaseline();
    Serial.println("raw_left,raw_center,raw_right,median_pressure_left,median_pressure_center,median_pressure_right,total_pressure,left_ratio,center_ratio,right_ratio,x_center_cm,moving,posture,confidence");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  analogReadResolution(12);
  analogSetPinAttenuation(FORCE_SENSOR_LEFT_PIN, ADC_11db);
  analogSetPinAttenuation(FORCE_SENSOR_CENTER_PIN, ADC_11db);
  analogSetPinAttenuation(FORCE_SENSOR_RIGHT_PIN, ADC_11db);

  calibrateBaseline();

  Serial.println("raw_left,raw_center,raw_right,median_pressure_left,median_pressure_center,median_pressure_right,total_pressure,left_ratio,center_ratio,right_ratio,x_center_cm,moving,posture,confidence");
}

void loop() {
  handleSerialCommand();

  const SensorRaw raw = readRawSensors();
  addPressureSample(raw);

  const unsigned long now = millis();
  if (windowCount >= WINDOW_SIZE && now - lastOutputMs >= OUTPUT_INTERVAL_MS) {
    lastOutputMs = now;
    const WindowStats stats = calculateWindowStats();
    const Features features = calculateFeatures(stats);
    printOutput(raw, stats, features);
  }

  delay(SAMPLE_INTERVAL_MS);
}
