cmake_minimum_required(VERSION 3.10.0)
project(pi-borescope-streamer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

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

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

find_program(CLANG_TIDY_EXE NAMES "clang-tidy" PATHS "/opt/homebrew/opt/llvm/bin" "/usr/local/opt/llvm/bin")
if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}" 
        "-checks=-*,performance-*,concurrency-*,bugprone-*,modernize-*,cppcoreguidelines-*,-modernize-use-trailing-return-type,-cppcoreguidelines-avoid-magic-numbers,-cppcoreguidelines-pro-bounds-pointer-arithmetic,-cppcoreguidelines-pro-type-reinterpret-cast,-cppcoreguidelines-avoid-c-arrays,-modernize-avoid-c-arrays,-cppcoreguidelines-pro-bounds-array-to-pointer-decay,-cppcoreguidelines-pro-type-cstyle-cast,-cppcoreguidelines-pro-type-vararg,-cppcoreguidelines-avoid-non-const-global-variables,-bugprone-throwing-static-initialization,-modernize-use-designated-initializers,-modernize-use-integer-sign-comparison,-modernize-use-auto,-modernize-use-ranges,-bugprone-narrowing-conversions,-cppcoreguidelines-narrowing-conversions,-modernize-avoid-c-style-cast,-cppcoreguidelines-pro-type-const-cast,-modernize-use-nodiscard,-modernize-use-equals-default,-cppcoreguidelines-special-member-functions,-cppcoreguidelines-pro-bounds-avoid-unchecked-container-access"
        "-warnings-as-errors=*"
    )
else()
    message(WARNING "clang-tidy not found")
endif()

find_program(CPPCHECK_EXE NAMES "cppcheck" PATHS "/opt/homebrew/bin" "/usr/local/bin")
if(CPPCHECK_EXE)
    set(CMAKE_CXX_CPPCHECK "${CPPCHECK_EXE}" 
        "--enable=all" 
        "--inconclusive" 
        "--suppress=missingIncludeSystem"
        "--suppress=unusedFunction"
        "--suppress=unmatchedSuppression"
        "--suppress=style" 
        "--inline-suppr"
        "--check-level=exhaustive"
    )
else()
    message(WARNING "cppcheck not found")
endif()

find_program(IWYU_EXE NAMES "include-what-you-use" PATHS "/opt/homebrew/bin" "/usr/local/bin")
if(IWYU_EXE)
    set(IWYU_ARGS "${IWYU_EXE}")

    # If compiling on Linux (Raspberry Pi), find and append the GCC mapping file
    if(NOT APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_file(GCC_MAP_FILE 
            NAMES stdcxx.imp
            PATHS "/usr/share/include-what-you-use" 
                  "/usr/local/share/include-what-you-use"
                  "/usr/share/llvm/include-what-you-use"
        )
        
        if(GCC_MAP_FILE)
            list(APPEND IWYU_ARGS "-Xiwyu" "--mapping_file=${GCC_MAP_FILE}")
        endif()
    endif()

    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "${IWYU_ARGS}")
else()
    message(WARNING "include-what-you-use not found")
endif()

add_library(project_warnings INTERFACE)

target_compile_options(project_warnings INTERFACE -Wall -Wextra)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(project_warnings INTERFACE 
        -Wthread-safety 
        -Werror=thread-safety
    )
endif()

if(NOT APPLE)
    target_compile_options(project_warnings INTERFACE 
        -g                        
        -fno-omit-frame-pointer   
    )
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        target_compile_options(project_warnings INTERFACE 
            -mcpu=cortex-a76 
            -mtune=cortex-a76
        )
    endif()
endif()

FetchContent_Declare(
    uWebSockets
    GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
    GIT_TAG v20.67.0
)
FetchContent_MakeAvailable(uWebSockets)

add_library(uwebsockets_headers INTERFACE)
target_include_directories(uwebsockets_headers SYSTEM INTERFACE "${uwebsockets_SOURCE_DIR}/src")
target_link_libraries(uwebsockets_headers INTERFACE ZLIB::ZLIB)

FetchContent_Declare(
    uSockets
    GIT_REPOSITORY https://github.com/uNetworking/uSockets.git
    GIT_TAG v0.8.8
)
FetchContent_MakeAvailable(uSockets)

add_library(uNetworking INTERFACE)
target_link_libraries(uNetworking INTERFACE uwebsockets_headers usockets ZLIB::ZLIB)

file(GLOB USOCKETS_SOURCES 
    "${usockets_SOURCE_DIR}/src/*.c" 
    "${usockets_SOURCE_DIR}/src/eventing/*.c"
)

add_library(usockets STATIC ${USOCKETS_SOURCES})
target_include_directories(usockets SYSTEM PUBLIC "${usockets_SOURCE_DIR}/src")
target_link_libraries(usockets PUBLIC OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB Threads::Threads)

if(APPLE)
    target_compile_definitions(usockets PRIVATE WITH_KQUEUE=1)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(usockets PRIVATE WITH_EPOLL=1)
endif()

target_compile_definitions(usockets PRIVATE LIBUS_NO_SSL=1)

add_library(borescope_core STATIC
    src/classes/buffer.cpp
    src/classes/buffer_pool.cpp
    src/classes/mjpeg_frame_extractor.cpp
    src/classes/mjpeg_frame_queue.cpp
    src/classes/mjpeg_frame_ring_buffer.cpp
    src/classes/mjpeg_server.cpp
    src/classes/mjpeg_stream.cpp
    src/classes/mjpeg_stream_capture.cpp
    src/classes/usb_camera.cpp 
    src/classes/usb_context.cpp
    src/classes/usb_device_finder.cpp
    src/classes/v4l2.cpp
    ${GENERATED_HPP}
)
add_library(Borescope::Core ALIAS borescope_core)

target_include_directories(borescope_core 
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(borescope_core 
    PUBLIC 
        PkgConfig::LIBUSB
        uNetworking
    PRIVATE 
        project_warnings
        GTest::gtest
)

add_executable(mjpeg_server
    src/apps/mjpeg_server_main.cpp
)
target_link_libraries(mjpeg_server PRIVATE Borescope::Core project_warnings)

add_executable(attached_usb_devices
    src/apps/attached_usb_devices.cpp
)
target_link_libraries(attached_usb_devices PRIVATE Borescope::Core project_warnings)

add_executable(mjpeg_stream_capture
    src/classes/mjpeg_stream_capture.cpp
    src/apps/mjpeg_stream_capture_main.cpp
)
target_link_libraries(mjpeg_stream_capture PRIVATE Borescope::Core project_warnings)

add_executable(mjpeg_frame_extractor
    src/classes/mjpeg_frame_extractor.cpp
    src/apps/mjpeg_frame_extractor_main.cpp
)
target_link_libraries(mjpeg_frame_extractor PRIVATE Borescope::Core project_warnings)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(borescope_core PRIVATE src/classes/v4l2_publisher.cpp)
    add_executable(v4l2-borescope-daemon
        src/classes/v4l2.cpp
        src/classes/v4l2_publisher.cpp
        src/apps/v4l2_main.cpp
    )
    target_link_libraries(v4l2-borescope-daemon
        PRIVATE 
            borescope_core
            project_warnings
            ${LIBUSB_LIBRARIES}
    )
    
    message(STATUS "Linux detected: Building v4l2-borescope-daemon target.")
else()
    message(STATUS "macOS detected: Skipping v4l2-borescope-daemon target.")
endif()

option(BUILD_DOC "Build documentation" ON)
if(BUILD_DOC)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(DOXYGEN_GENERATE_HTML YES)
        set(DOXYGEN_GENERATE_LATEX NO)
        set(DOXYGEN_BUILTIN_STL_SUPPORT YES)
        set(DOXYGEN_EXTRACT_ALL YES)
        set(DOXYGEN_EXTRACT_PRIVATE YES)
        set(DOXYGEN_EXTRACT_STATIC YES)
        set(DOXYGEN_RECURSIVE YES)
        set(DOXYGEN_ENABLE_PREPROCESSING YES)
        set(DOXYGEN_MACRO_EXPANSION YES)
        set(DOXYGEN_PREDEFINED "[[gnu::packed]]=")
        set(DOXYGEN_HAVE_DOT YES)
        set(DOXYGEN_DOT_NUM_THREADS 0)

        doxygen_add_docs(docs 
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/include
            COMMENT "Generating API documentation"
        )
    else()
        message(WARNING "Doxygen not found. Documentation target will not be created.")
    endif()
endif()

option(BUILD_TESTING "Build the test suite" ON)
if(BUILD_TESTING)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP true
        SYSTEM
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    set_target_properties(gtest gtest_main gmock gmock_main PROPERTIES
        CXX_CLANG_TIDY ""
        CXX_INCLUDE_WHAT_YOU_USE ""
        CXX_CPPCHECK ""
    )

    target_compile_options(gtest PRIVATE -w)
    target_compile_options(gtest_main PRIVATE -w)
    target_compile_options(gmock PRIVATE -w)
    target_compile_options(gmock_main PRIVATE -w)

    target_link_libraries(borescope_core INTERFACE gtest)

    add_executable(run_project_tests 
        tests/test_buffer_pool.cpp
        tests/test_mjpeg_frame_queue.cpp
        tests/test_mjpeg_server.cpp
        tests/test_mjpeg_stream.cpp
        tests/test_usb_device_finder.cpp
        tests/test_v4l2.cpp
    )

    target_include_directories(run_project_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

    target_link_libraries(run_project_tests 
        PRIVATE 
            borescope_core
            project_warnings
            gtest_main
            gmock
    )

    include(GoogleTest)
    gtest_discover_tests(run_project_tests)
endif()

option(ENABLE_COVERAGE "Enable code coverage tracking" OFF)
if(ENABLE_COVERAGE)
    target_compile_options(borescope_core PUBLIC --coverage)
    target_link_options(borescope_core PUBLIC --coverage)
endif()
