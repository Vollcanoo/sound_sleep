# Local ESP-DL Dependencies

This directory is intentionally not committed because ESP-DL and ESP-DSP add
large third-party source trees. The firmware manifest uses these exact paths:

```text
esp-dl/esp-dl/CMakeLists.txt
esp-dl/tools/dl_fft/CMakeLists.txt
esp-dsp/CMakeLists.txt
esp_new_jpeg/CMakeLists.txt
```

From the repository root, restore them with:

```powershell
New-Item -ItemType Directory -Force .\Snore_Det\third_party | Out-Null
git clone --depth 1 --branch v3.3.8 https://github.com/espressif/esp-dl.git .\Snore_Det\third_party\esp-dl
git clone --depth 1 --branch v1.7.0 https://github.com/espressif/esp-dsp.git .\Snore_Det\third_party\esp-dsp
git clone --depth 1 --branch v1.0.2 https://github.com/espressif/esp_new_jpeg.git .\Snore_Det\third_party\esp_new_jpeg
```

After the folders exist, run `idf.py build` from the repository root. Do not
remove the `override_path` entries in `main/idf_component.yml`; they avoid a
network component-registry download during project configuration.
