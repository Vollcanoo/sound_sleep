# Posture Recognition

This folder contains the ESP32-S3 firmware for pillow-based sleep posture recognition using three FSR film pressure sensors.

## Hardware Setup

- Microcontroller: ESP32-S3
- Sensors: 3 FSR film pressure sensors
- Default ADC pins:
  - Left sensor: GPIO4
  - Center sensor: GPIO5
  - Right sensor: GPIO6
- Serial baud rate: 115200

## Recommended Sensor Placement

For a 15 cm x 15 cm pillow, place the sensors under the pillow as follows:

```text
      Back / head side
          [CENTER]

    [LEFT]       [RIGHT]
      Front / shoulder side
```

Approximate left-to-right positions:

- Left: x = -5 cm
- Center: x = 0 cm
- Right: x = +5 cm

The firmware uses the left-to-right pressure distribution. The center sensor should remain between the left and right sensors.

## Firmware Behavior

The firmware:

- calibrates the unloaded baseline at startup;
- samples the three FSR channels at 10 Hz;
- uses a short median window instead of instant readings;
- rejects unstable windows as `MOVING`;
- computes normalized pressure ratios and pressure center;
- outputs posture classification once per second.

Possible posture outputs:

- `NO_HEAD`
- `MOVING`
- `LEFT_SIDE`
- `RIGHT_SIDE`
- `SUPINE`

## Serial Output

The output is CSV-formatted:

```text
raw_left,raw_center,raw_right,median_pressure_left,median_pressure_center,median_pressure_right,total_pressure,left_ratio,center_ratio,right_ratio,x_center_cm,moving,posture,confidence
```

Send `b` or `B` over serial to recalibrate the unloaded baseline.

## Limitations

With only three FSR sensors under the pillow, posture recognition is heuristic rather than medical-grade. Left-side and right-side posture are inferred from lateral pressure imbalance. When a stable head-pressure distribution is detected but neither side is dominant, the firmware classifies it as `SUPINE`. Prone posture is not classified.
