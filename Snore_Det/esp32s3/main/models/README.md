# Model Placement

The INT8 model is intentionally excluded from Git.

After training and quantization, place the generated model at:

    esp32s3/main/models/snoring_esp32_int8.espdl

The ESP-IDF build embeds this file into the firmware.
