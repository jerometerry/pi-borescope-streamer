cmake_minimum_required(VERSION 3.10.0)
project(pi-borescope-streamer VERSION 0.1.0 LANGUAGES C CXX)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache found and enabled")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(KERNEL_DRIVER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../useeplus_v4l2")

# --- Build Configuration Options ---
option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis during build" OFF)
option(ENABLE_IWYU "Enable include-what-you-use analysis during build" OFF)
option(ENABLE_CPPCHECK "Create a target for cppcheck" OFF)
option(BUILD_DOC "Build documentation" OFF)
option(BUILD_TESTING "Build the test suite" OFF)
option(BUILD_BENCHMARKS "Build benchmark executables" OFF)
option(ENABLE_COVERAGE "Enable code coverage tracking" OFF)

include(FetchContent)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)

if(APPLE)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
endif()

# --- HTML to Header Conversion ---
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

# --- Static Analysis (Opt-In) ---
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy"
        PATHS "/opt/homebrew/opt/llvm/bin" "/usr/bin" "/usr/local/bin"
    )
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
            "-checks=-*,performance-*,concurrency-*,bugprone-*,modernize-*,cppcoreguidelines-*,-modernize-use-trailing-return-type,-cppcoreguidelines-avoid-magic-numbers,-cppcoreguidelines-pro-bounds-pointer-arithmetic,-cppcoreguidelines-pro-type-reinterpret-cast,-cppcoreguidelines-avoid-c-arrays,-modernize-avoid-c-arrays,-cppcoreguidelines-pro-bounds-array-to-pointer-decay,-cppcoreguidelines-pro-type-cstyle-cast,-cppcoreguidelines-pro-type-vararg,-cppcoreguidelines-avoid-non-const-global-variables,-bugprone-throwing-static-initialization,-modernize-use-designated-initializers,-modernize-use-integer-sign-comparison,-modernize-use-auto,-modernize-use-ranges,-bugprone-narrowing-conversions,-cppcoreguidelines-narrowing-conversions,-modernize-avoid-c-style-cast,-cppcoreguidelines-pro-type-const-cast,-modernize-use-nodiscard,-modernize-use-equals-default,-cppcoreguidelines-special-member-functions,-cppcoreguidelines-pro-bounds-avoid-unchecked-container-access"
            "-warnings-as-errors=*"
        )
    else()
        message(WARNING "clang-tidy enabled but not found!")
    endif()
endif()

if(ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXE NAMES "cppcheck"
        PATHS "/opt/homebrew/bin" "/usr/bin" "/usr/local/bin"
    )

    find_program(CPPCHECK_HTML_EXE NAMES "cppcheck-htmlreport"
        PATHS "/opt/homebrew/bin" "/usr/bin" "/usr/local/bin" "$ENV{HOME}/.local/bin"
    )

    if(CPPCHECK_EXE)
        set(CPPCHECK_XML_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        set(CPPCHECK_HTML_DIR "${CMAKE_CURRENT_BINARY_DIR}/html_report")

        add_custom_target(run_cppcheck
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CPPCHECK_XML_DIR}/cppcheck_build"
            COMMAND sh -c "\"${CPPCHECK_EXE}\" --project=\"${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json\" --cppcheck-build-dir=\"${CPPCHECK_XML_DIR}/cppcheck_build\" -i\"${CMAKE_CURRENT_BINARY_DIR}/_deps\" --suppress=*:*googletest* --suppress=*:*gmock* --xml --enable=all --inconclusive --inline-suppr --check-level=exhaustive --suppress=missingIncludeSystem --suppress=unmatchedSuppression 2> \"${CPPCHECK_XML_DIR}/cppcheck_report.xml\""
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Running single-pass whole-program Cppcheck analysis"
            VERBATIM
        )

        if(CPPCHECK_HTML_EXE)
            add_custom_command(
                TARGET run_cppcheck POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CPPCHECK_HTML_DIR}"
                COMMAND "${CPPCHECK_HTML_EXE}"
                    --file=${CPPCHECK_XML_DIR}/cppcheck_report.xml
                    --report-dir=${CPPCHECK_HTML_DIR}
                    --source-dir=${PROJECT_SOURCE_DIR}/src
                    --title=${PROJECT_NAME}
                COMMENT "Converting Cppcheck XML to HTML report..."
                VERBATIM
            )
        else()
            message(WARNING "cppcheck-htmlreport not found. Only the XML report will be generated.")
        endif()

    else()
        message(WARNING "cppcheck enabled but not found!")
    endif()
endif()

if(ENABLE_IWYU)
    find_program(IWYU_EXE NAMES "include-what-you-use" iwyu
        PATHS "/opt/homebrew/bin" "/usr/bin" "/usr/local/bin"
    )
    if(IWYU_EXE)
        set(IWYU_ARGS "${IWYU_EXE}")
        set(LOCAL_MAP "${CMAKE_CURRENT_SOURCE_DIR}/project.imp")
        if(EXISTS "${LOCAL_MAP}")
            list(APPEND IWYU_ARGS "-Xiwyu" "--mapping_file=${LOCAL_MAP}")
        endif()

        if(NOT APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_file(GCC_MAP_FILE
                NAMES stdcxx.imp
                PATHS "/usr/share/include-what-you-use"
                      "/usr/local/share/include-what-you-use"
                      "/usr/share/llvm/include-what-you-use"
            )
            if(GCC_MAP_FILE)
                list(APPEND IWYU_ARGS
                    "-Xiwyu" "--mapping_file=${GCC_MAP_FILE}"
                    "-Xiwyu" "--no_default_mappings"
                )
            endif()
            list(APPEND IWYU_ARGS
                "-Xiwyu" "--no_fwd_decls"
                "-Xiwyu" "--no_comments"
                "-Qunused-arguments"
            )
        endif()
        set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "${IWYU_ARGS}")
    else()
        message(WARNING "include-what-you-use enabled but not found!")
    endif()
endif()

# --- Warning & Compiler Flags ---
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

option(ENABLE_SANITIZERS "Enable Address and Undefined Behavior Sanitizers" OFF)

if(ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(project_warnings INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(project_warnings INTERFACE
        -fsanitize=address,undefined
    )
endif()

# --- Dependencies: uWebSockets & uSockets ---
FetchContent_Declare(
    uWebSockets
    GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
    GIT_TAG v20.67.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(uWebSockets)

add_library(uwebsockets_headers INTERFACE)
target_include_directories(uwebsockets_headers SYSTEM INTERFACE "${uwebsockets_SOURCE_DIR}/src")
target_link_libraries(uwebsockets_headers INTERFACE ZLIB::ZLIB)

FetchContent_Declare(
    uSockets
    GIT_REPOSITORY https://github.com/uNetworking/uSockets.git
    GIT_TAG v0.8.8
    GIT_SHALLOW TRUE
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

# --- Core Library ---
add_library(borescope_core STATIC
    src/mjpeg_server.cpp
    src/v4l2_mjpeg_stream.cpp
    src/zero_allocation_response_builder.cpp
    ${GENERATED_HPP}
)
add_library(Borescope::Core ALIAS borescope_core)

if(APPLE)
    target_sources(borescope_core PRIVATE
        src/mjpeg_stream_capture.cpp
        src/usb_camera.cpp
        src/usb_context.cpp
        src/usb_device_finder.cpp
        src/usb_driver.cpp
        src/mjpeg_stream.cpp
        ${KERNEL_DRIVER_DIR}/useeplus_protocol.c
    )
    target_link_libraries(borescope_core PUBLIC PkgConfig::LIBUSB)
endif()

target_include_directories(borescope_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PUBLIC "${KERNEL_DRIVER_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(borescope_core PUBLIC uNetworking PRIVATE project_warnings)

if(ENABLE_COVERAGE)
    target_compile_options(borescope_core PUBLIC --coverage)
    target_link_options(borescope_core PUBLIC --coverage)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Linux detected: Building v4l2 targets.")

    add_executable(v4l2_mjpeg_server
        src/v4l2_mjpeg_server_main.cpp
    )
    target_link_libraries(v4l2_mjpeg_server
        PRIVATE Borescope::Core project_warnings
    )
elseif(APPLE)
    message(STATUS "MacOS detected: Building libusb targets.")

    add_executable(mjpeg_server src/mjpeg_server_main.cpp)
    target_link_libraries(mjpeg_server PRIVATE Borescope::Core project_warnings)

    add_executable(mjpeg_stream_capture src/mjpeg_stream_capture_main.cpp)
    target_link_libraries(mjpeg_stream_capture PRIVATE Borescope::Core project_warnings)

    add_executable(mjpeg_frame_extractor src/mjpeg_frame_extractor_main.cpp)
    target_link_libraries(mjpeg_frame_extractor PRIVATE Borescope::Core project_warnings)
endif()

# --- Documentation (Opt-In) ---
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

# --- Tests (Opt-In) ---
if(BUILD_TESTING)
    enable_testing()
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

    target_link_libraries(borescope_core PRIVATE GTest::gtest)

    add_executable(run_project_tests
        tests/test_disruptor.cpp
        tests/test_mjpeg_server.cpp
        tests/test_mjpeg_stream.cpp
        tests/test_usb_device_finder.cpp
        tests/test_zero_allocation_response_builder.cpp
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

    # --- Linux Driver Tests ---

    add_executable(run_linux_driver_tests
        "${KERNEL_DRIVER_DIR}/tests/test_useeplus_decoder.cpp"
	"${KERNEL_DRIVER_DIR}/tests/test_useeplus_protocol.cpp"
	"${KERNEL_DRIVER_DIR}/useeplus_protocol.c"
    )

    target_include_directories(run_linux_driver_tests PRIVATE
        "${KERNEL_DRIVER_DIR}/tests/mock_kernel"
        "${KERNEL_DRIVER_DIR}"
    )

    target_link_libraries(run_linux_driver_tests
        PRIVATE
            project_warnings
            gtest_main
            gmock
    )

    # Disable static analysis for the kernel source since it uses C/kernel idioms
    # that will fail C++ user-space linting rules.
    set_target_properties(run_linux_driver_tests PROPERTIES
        C_CLANG_TIDY ""
        CXX_CLANG_TIDY ""
        CXX_INCLUDE_WHAT_YOU_USE ""
        CXX_CPPCHECK ""
        C_CPPCHECK ""
    )

    if(ENABLE_COVERAGE)
        target_compile_options(run_linux_driver_tests PRIVATE --coverage)
        target_link_options(run_linux_driver_tests PRIVATE --coverage)
    endif()

    gtest_discover_tests(run_linux_driver_tests)
endif()

# --- Benchmarks (Opt-In) ---
if(BUILD_BENCHMARKS)
    FetchContent_Declare(
      googlebenchmark
      URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP true
      SYSTEM
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)

    set_target_properties(benchmark benchmark_main PROPERTIES
        CXX_CLANG_TIDY ""
        CXX_INCLUDE_WHAT_YOU_USE ""
        CXX_CPPCHECK ""
    )

    add_executable(disruptor.bench benchmark/disruptor_benchmark.cpp)
    target_link_libraries(disruptor.bench
        PRIVATE Borescope::Core benchmark::benchmark_main Threads::Threads project_warnings
    )

    add_executable(disruptor_concurrency.bench benchmark/disruptor_concurrency_benchmark.cpp)
    target_link_libraries(disruptor_concurrency.bench
        PRIVATE Borescope::Core benchmark::benchmark_main Threads::Threads project_warnings
    )

    add_executable(mjpeg_pipeline.bench benchmark/mjpeg_pipeline_benchmark.cpp)
    target_link_libraries(mjpeg_pipeline.bench
        PRIVATE Borescope::Core benchmark::benchmark_main Threads::Threads project_warnings
    )
endif()

find_program(CLANG_FORMAT_EXE NAMES "clang-format")
if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE FORMAT_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../useeplus_v4l2/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../useeplus_v4l2/tests/mock_kernel/**/*.h"
    )

    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXE}" -i ${FORMAT_SRCS}
        COMMENT "Formatting C++ code with clang-format"
        VERBATIM
    )

    add_custom_target(check-format
        COMMAND "${CLANG_FORMAT_EXE}" --dry-run --Werror ${FORMAT_SRCS}
        COMMENT "Checking C++ code formatting"
        VERBATIM
    )
endif()