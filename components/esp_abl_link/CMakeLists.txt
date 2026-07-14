idf_component_register(
    SRCS "vendor/ableton-link/extensions/abl_link/src/abl_link.cpp"
    INCLUDE_DIRS "vendor/ableton-link/extensions/abl_link/include"
    REQUIRES asio
    PRIV_REQUIRES sock_utils esp_timer esp_netif 
)

# C++11 is required for Ableton Link - use specific feature like the official example
# Using PRIVATE since only our wrapper needs C++11, not consumers
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_generalized_initializers)

# Ableton Link include directories (internal, abl_link.cpp needs Link.hpp)
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/vendor/ableton-link/include"
)

# ESP_PLATFORM is automatically defined by ESP-IDF, which Link uses to detect ESP32

# Define required ESP32 platform configuration for Ableton Link
# LINK_ESP_TASK_CORE_ID: Which core to run Link tasks on (configurable via Kconfig)
# -1 means tskNO_AFFINITY, 0/1 for specific cores
# When FREERTOS_UNICORE is enabled, the Kconfig option is hidden, so we default to tskNO_AFFINITY
if(DEFINED CONFIG_LINK_ESP_TASK_CORE_ID)
    if(CONFIG_LINK_ESP_TASK_CORE_ID EQUAL -1)
        set(LINK_ESP_TASK_CORE_ID tskNO_AFFINITY)
    else()
        set(LINK_ESP_TASK_CORE_ID ${CONFIG_LINK_ESP_TASK_CORE_ID})
    endif()
else()
    set(LINK_ESP_TASK_CORE_ID tskNO_AFFINITY)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE LINK_ESP_TASK_CORE_ID=${LINK_ESP_TASK_CORE_ID})

# Suppress return-type warnings for Link code (ASIO_NO_EXCEPTIONS causes unreachable code paths)
# Link doesn't fully support no-exceptions mode
# Add -fexceptions explicitly like the official ESP32 example
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-return-type
    -fexceptions
)

# ESP-IDF component manager can sometimes mess up the link order when multiple components
# are involved. To fix link errors caused by that, we use --start-group/--end-group around
# standard libraries to make sure all symbols are resolved correctly.
target_link_libraries(${COMPONENT_LIB} INTERFACE 
    "-Wl,--start-group" "-lstdc++" "-lc" "-Wl,--end-group")

# Link atomic library for RISC-V targets (ESP32-C3, C6, H2, etc.) which need runtime
# support for 64-bit atomic operations like __atomic_is_lock_free.
# Xtensa targets (ESP32, ESP32-S2, ESP32-S3) don't need this.
if(CONFIG_IDF_TARGET_ARCH_RISCV)
    target_link_libraries(${COMPONENT_LIB} PRIVATE atomic)
endif()
