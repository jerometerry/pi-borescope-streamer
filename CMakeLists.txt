cmake_minimum_required(VERSION 3.10.0)
project(pi-borescope-streamer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
    )
endif()

find_program(IWYU_EXE NAMES "include-what-you-use" PATHS "/opt/homebrew/bin" "/usr/local/bin")
if(IWYU_EXE)
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "${IWYU_EXE}")
endif()

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE -Wall -Wextra)

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

add_library(useeplus_protocol STATIC 
    src/usb_context.cpp
    src/device_finder.cpp
    src/usb_camera.cpp 
    src/usb_frame_decoder.cpp
    src/usb_frame_decoder_v2.cpp
)
target_include_directories(useeplus_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(useeplus_protocol PUBLIC PkgConfig::LIBUSB)
target_link_libraries(useeplus_protocol PRIVATE project_warnings)

add_library(mjpeg_streamer STATIC 
    src/mjpeg_stream.cpp
    src/web_server.cpp
    src/server_time.cpp
    ${GENERATED_HPP}
)
target_include_directories(mjpeg_streamer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(mjpeg_streamer PUBLIC useeplus_protocol)
target_link_libraries(mjpeg_streamer PRIVATE project_warnings)

add_executable(pi-borescope-streamer 
    src/server.cpp
)
target_link_libraries(pi-borescope-streamer PRIVATE mjpeg_streamer project_warnings)

add_executable(supercamera
    src/supercamera.cpp 
)
target_link_libraries(supercamera PRIVATE mjpeg_streamer project_warnings)

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
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
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

    add_executable(run_project_tests 
        tests/test_useeplus_protocol.cpp
        tests/test_mjpeg_stream.cpp
        tests/test_mjpeg_streamer.cpp
        tests/test_web_server.cpp
    )

    target_include_directories(run_project_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

    target_link_libraries(run_project_tests 
        PRIVATE 
            mjpeg_streamer 
            project_warnings 
            GTest::gtest_main
            GTest::gmock
    )

    include(GoogleTest)
    gtest_discover_tests(run_project_tests)
endif()

option(ENABLE_COVERAGE "Enable code coverage tracking" OFF)
if(ENABLE_COVERAGE)
    target_compile_options(useeplus_protocol PUBLIC --coverage)
    target_link_options(useeplus_protocol PUBLIC --coverage)
    
    target_compile_options(mjpeg_streamer PUBLIC --coverage)
    target_link_options(mjpeg_streamer PUBLIC --coverage)
endif()