# Snore Model

Place the current Snore_Det quantized model here as:

`snoring_esp32_int8.espdl`

The file is intentionally not committed. When it is present, CMake embeds it
and enables INMP441 inference. Without it, the firmware builds and runs posture
monitoring only.
