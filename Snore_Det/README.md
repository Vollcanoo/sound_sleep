# Snore Detector

This directory contains the reproducible logic for snore classification and
ESP32-S3 deployment. Datasets and trained model artifacts are intentionally
excluded.

## Pipeline

1. tools/fea_extra.py resamples WAV audio to 16 kHz and creates 64 x 64
   Log-Mel features.
2. train_esp32_gpu.py trains the lightweight network with CUDA and exports ONNX.
3. quantize_esp32.py creates an ESP-DL INT8 model with ESP-PPQ.
4. esp32s3 captures INMP441 audio, creates the same Log-Mel feature, and runs
   inference on an ESP32-S3 N8R8.

## Training

    pip install -r requirements.txt
    python tools/fea_extra.py
    python train_esp32_gpu.py --data-root Snoring_Dataset/fea
    python quantize_esp32.py --feature-root Snoring_Dataset/fea

Copy the generated INT8 model to esp32s3/main/models before building firmware.

## ESP32-S3

Tested INMP441 wiring is BCLK/SCK GPIO16, WS/LRCLK GPIO15, DOUT/SD GPIO17, with
L/R tied to GND and the right I2S slot selected.

Install dependencies from third_party/README.md, then build using ESP-IDF v5.3
or newer:

    cd esp32s3
    . $IDF_PATH/export.sh
    idf.py set-target esp32s3
    idf.py build

The legacy I2S API is retained because it mirrors the board-verified recorder.
Its deprecation warning is expected.
