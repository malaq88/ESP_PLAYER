# ESP32-A2DP — local component (A2DP Source + callbacks only)

idf_component_register(
    SRCS
        "src/BluetoothA2DPCommon.cpp"
        "src/BluetoothA2DPSource.cpp"
    INCLUDE_DIRS src
    REQUIRES bt esp_common freertos hal log nvs_flash driver esp_ringbuf esp_timer
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -DA2DP_LEGACY_I2S_SUPPORT=0
    -DA2DP_I2S_AUDIOTOOLS=0
    -DA2DP_SPP_SUPPORT=0
    -Wno-error
    -Wno-format
    -fpermissive
)
