set(srcs app_main.cpp audio_capture.cpp log_mel.cpp)
set(requires esp-dl esp-dsp driver)

# The project pins ESP-DL to third_party through idf_component.yml.
# Resolve it directly so component-manager target aliases cannot affect model embedding.
set(espdl_dir "${CMAKE_CURRENT_LIST_DIR}/../../third_party/esp-dl/esp-dl")
set(cmake_dir "${espdl_dir}/fbs_loader/cmake")
include(${espdl_dir}/fbs_loader/cmake/utilities.cmake)
set(embed_file models/snoring_esp32_int8.espdl)

idf_component_register(SRCS ${srcs} REQUIRES ${requires})
target_add_aligned_binary_data(${COMPONENT_LIB} ${embed_file} BINARY)
