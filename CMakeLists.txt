cmake_minimum_required(VERSION 3.10.0)
project(pi-borescope-streamer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Asset Generation Layout ---
set(HTML_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/index.html")
set(GENERATED_HPP "${CMAKE_CURRENT_BINARY_DIR}/generated/index_html.hpp")

add_custom_command(
    OUTPUT "${GENERATED_HPP}"
    COMMAND ${CMAKE_COMMAND} 
        -D INPUT_HTML="${HTML_FILE}"
        -D OUTPUT_HPP="${GENERATED_HPP}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/GenerateHeader.cmake"
    DEPENDS "${HTML_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/GenerateHeader.cmake"
    COMMENT "Converting ${HTML_FILE} to C++ header format"
)

# --- Package Dependency Management ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

# --- Main Executable Definition ---
add_executable(pi-borescope-streamer 
    src/main.cpp 
    src/mjpeg_stream.cpp 
    src/usb_camera.cpp 
    src/usb_camera_protocol.cpp 
    src/web_server.cpp 
    ${GENERATED_HPP}
)

target_include_directories(pi-borescope-streamer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(pi-borescope-streamer PRIVATE PkgConfig::LIBUSB)

# ==============================================================================
# Ported Flags from Makefile
# ==============================================================================

# Global flags for all platforms
target_compile_options(pi-borescope-streamer PRIVATE -Wall -Wextra)

# Check for platform types natively via CMake variables
if(APPLE)
    # macOS Specifics (Homebrew is handled automatically via PkgConfig searching)
    # No extra flags needed here as Clang handles optimizations smoothly
else()
    # Linux Specific Configuration
    target_compile_options(pi-borescope-streamer PRIVATE 
        -g                        # Generate debug symbols
        -fno-omit-frame-pointer   # Preserved: Keep frame pointers for debugging/profiling
    )

    # Detect if we are building on a Raspberry Pi 5 / ARM64 platform
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        target_compile_options(pi-borescope-streamer PRIVATE 
            -mcpu=cortex-a76 
            -mtune=cortex-a76
        )
    endif()
endif()
