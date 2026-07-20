# ESP-IDF Dependencies

ESP-DL and ESP-DSP are not copied into this repository.

Clone both dependencies before building:

    cd Snore_Det
    git clone --depth 1 https://github.com/espressif/esp-dl.git third_party/esp-dl
    git clone --depth 1 https://github.com/espressif/esp-dsp.git third_party/esp-dsp

Those locations match esp32s3/main/idf_component.yml.
