.PHONY: all clean format check-format test tidy cppcheck iwyu docs sanitize \
	benchmark install-deps christmas-tree

CXX := zig c++
CC  := zig cc

ifneq (,$(shell command -v ccache 2>/dev/null))
    CXX := ccache $(CXX)
    CC  := ccache $(CC)
endif

CXXFLAGS := -std=c++2b -Wall -Wextra -g -fno-omit-frame-pointer -Wno-deprecated-declarations
CFLAGS   := -Wall -Wextra -g -fno-omit-frame-pointer

SRC_DIR          := src
INC_DIR          := include
GEN_DIR          := generated
LINUX_DRIVER_DIR := ../linux_driver
BUILD_DIR        := build
TEST_DIR         := tests
BENCH_DIR        := benchmark

HTML_FILE     := $(SRC_DIR)/index.html
GENERATED_HPP := $(GEN_DIR)/index_html.hpp

CORE_SRCS := $(SRC_DIR)/mjpeg_server.cpp \
             $(SRC_DIR)/http_response_builder.cpp \
             $(SRC_DIR)/libusb/usb_camera.cpp \
             $(SRC_DIR)/libusb/usb_context.cpp \
             $(SRC_DIR)/libusb/usb_device_finder.cpp \
             $(SRC_DIR)/libusb/usb_driver.cpp \
             $(SRC_DIR)/libusb/useeplus_video_stream.cpp \
             $(LINUX_DRIVER_DIR)/useeplus_protocol.c

CORE_OBJS := $(patsubst %,$(BUILD_DIR)/%.o,$(basename $(CORE_SRCS)))
CORE_LIB  := $(BUILD_DIR)/libmjpeg_streamer_core.a

TARGETS := $(BUILD_DIR)/mjpeg_server

PROJECT_TEST_SRCS := $(TEST_DIR)/test_disruptor.cpp \
                     $(TEST_DIR)/test_mjpeg_server.cpp \
                     $(TEST_DIR)/test_useeplus_video_stream.cpp \
                     $(TEST_DIR)/test_http_response_builder.cpp

DRIVER_TEST_SRCS  := $(LINUX_DRIVER_DIR)/tests/test_useeplus_decoder.cpp \
                     $(LINUX_DRIVER_DIR)/tests/test_useeplus_protocol.cpp

PROJECT_TEST_OBJS := $(patsubst %,$(BUILD_DIR)/%.o,$(basename $(PROJECT_TEST_SRCS)))
DRIVER_TEST_OBJS  := $(patsubst %,$(BUILD_DIR)/%.o,$(basename $(DRIVER_TEST_SRCS)))
TEST_TARGETS      := $(BUILD_DIR)/run_project_tests $(BUILD_DIR)/run_linux_driver_tests

BENCHMARK_SRCS    := $(BENCH_DIR)/disruptor_benchmark.cpp \
                     $(BENCH_DIR)/disruptor_concurrency_benchmark.cpp \
                     $(BENCH_DIR)/mjpeg_pipeline_benchmark.cpp

BENCHMARK_OBJS    := $(patsubst %,$(BUILD_DIR)/%.o,$(basename $(BENCHMARK_SRCS)))
BENCHMARK_TARGETS := $(patsubst $(BENCH_DIR)/%.cpp,$(BUILD_DIR)/%.bench,$(BENCHMARK_SRCS))

UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -p)

ifeq ($(UNAME_S),Linux)
    PLATFORM := LINUX
    CXXFLAGS += -pthread
    LDFLAGS  += -pthread
    TARGETS  += $(BUILD_DIR)/v4l2_mjpeg_server
    ifneq (,$(filter aarch64%,$(UNAME_P)))
        CXXFLAGS += -target aarch64-linux-gnu -mcpu=cortex-a76
        CFLAGS   += -target aarch64-linux-gnu -mcpu=cortex-a76
    endif
else ifeq ($(UNAME_S),Darwin)
    PLATFORM := MACOS
endif

LIBUSB_CFLAGS := $(shell pkg-config --cflags libusb-1.0)

INCLUDES := -I$(INC_DIR) \
            -I$(INC_DIR)/libusb \
            -I$(INC_DIR)/v4l2 \
            -I$(LINUX_DRIVER_DIR) \
            -I$(GEN_DIR) \
            -isystem $(BUILD_DIR)/sockets/uWebSockets/src \
            -isystem $(BUILD_DIR)/sockets/uSockets/src \
            $(patsubst -I%,-isystem %,$(LIBUSB_CFLAGS))

LDFLAGS += -Wl,--start-group \
           $(BUILD_DIR)/sockets/uSockets/uSockets.a \
           $(BUILD_DIR)/libmjpeg_streamer_core.a \
           -Wl,--end-group -lz -lssl -lcrypto -lpthread \
           $(shell pkg-config --libs libusb-1.0)

ifeq ($(PLATFORM),MACOS)
    OSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null || echo "-I/opt/homebrew/opt/openssl@3/include")
    INCLUDES += $(patsubst -I%,-isystem %,$(OSSL_CFLAGS))
    LDFLAGS  += $(shell pkg-config --libs-only-L openssl 2>/dev/null || echo "-L/opt/homebrew/opt/openssl@3/lib")
endif

GTEST_INC  := -isystem build/googletest/googletest/include \
              -isystem build/googletest/googlemock/include
GTEST_LIBS := build/googletest/build/lib/libgtest.a \
              build/googletest/build/lib/libgtest_main.a \
              build/googletest/build/lib/libgmock.a

BENCH_INC  := -isystem build/benchmark/include
BENCH_LIBS := build/benchmark/build/src/libbenchmark.a \
              build/benchmark/build/src/libbenchmark_main.a

$(PROJECT_TEST_OBJS) $(DRIVER_TEST_OBJS): INCLUDES += $(GTEST_INC)
$(DRIVER_TEST_OBJS): INCLUDES += -I$(LINUX_DRIVER_DIR)
$(BENCHMARK_OBJS): INCLUDES += $(BENCH_INC)

all: $(CORE_DEPS) $(TARGETS)

clean:
	rm -rf $(BUILD_DIR) $(GEN_DIR)

CORE_DEPS     := $(BUILD_DIR)/.core_deps
TEST_DEPS     := $(BUILD_DIR)/.test_deps
BENCH_DEPS    := $(BUILD_DIR)/.bench_deps
DEPS_SENTINEL := $(BUILD_DIR)/.deps_installed

$(CORE_OBJS): | $(CORE_DEPS)
$(PROJECT_TEST_OBJS) $(DRIVER_TEST_OBJS): | $(CORE_DEPS) $(TEST_DEPS)
$(BENCHMARK_OBJS): | $(CORE_DEPS) $(BENCH_DEPS)

$(CORE_DEPS): install-core-deps.sh
	@echo "Checking core dependency trees..."
	@mkdir -p $(BUILD_DIR)
	@./install-core-deps.sh
	@touch $@

$(TEST_DEPS): install-test-deps.sh
	@echo "Checking test dependency trees..."
	@mkdir -p $(BUILD_DIR)
	@./install-test-deps.sh
	@touch $@

$(BENCH_DEPS): install-bench-deps.sh
	@echo "Checking benchmark dependency trees..."
	@mkdir -p $(BUILD_DIR)
	@./install-bench-deps.sh
	@touch $@

$(DEPS_SENTINEL): install-deps.sh
	@echo "Checking project dependency trees..."
	@./install-deps.sh
	@mkdir -p $(BUILD_DIR)
	@touch $@

install-deps: $(DEPS_SENTINEL)

$(GENERATED_HPP): $(HTML_FILE)
	@mkdir -p $(GEN_DIR)
	@echo "/* Generated by Makefile - do not modify */" > $@
	@echo "#pragma once" >> $@
	@echo "#include <string_view>" >> $@
	@echo "" >> $@
	@echo "namespace Resources {" >> $@
	@echo "    inline constexpr char index_html_data[] = {" >> $@
	@hexdump -v -e '/1 "0x%02x, "' $< >> $@
	@echo "" >> $@
	@echo "    };" >> $@
	@echo "    inline constexpr std::string_view index_html(index_html_data, sizeof(index_html_data));" >> $@
	@echo "}" >> $@

$(BUILD_DIR)/%.o: %.cpp $(GENERATED_HPP)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: %.c $(GENERATED_HPP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(CORE_LIB): $(CORE_OBJS)
	@echo "Archiving $@"
	@ar rcs $@ $^

$(BUILD_DIR)/v4l2_mjpeg_server: $(BUILD_DIR)/src/v4l2/v4l2_mjpeg_server_main.o $(CORE_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/mjpeg_server: $(BUILD_DIR)/src/libusb/mjpeg_server_main.o $(CORE_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/run_project_tests: $(PROJECT_TEST_OBJS) $(CORE_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(GTEST_LIBS) $(LDFLAGS)

$(BUILD_DIR)/run_linux_driver_tests: $(DRIVER_TEST_OBJS) $(CORE_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(GTEST_LIBS) $(LDFLAGS)

$(BUILD_DIR)/%.bench: $(BUILD_DIR)/benchmark/%.o $(CORE_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(BENCH_LIBS) $(LDFLAGS)

test: $(CORE_DEPS) $(TEST_DEPS) $(TEST_TARGETS)
	@$(BUILD_DIR)/run_project_tests
	@$(BUILD_DIR)/run_linux_driver_tests

benchmark: $(CORE_DEPS) $(BENCH_DEPS) $(BENCHMARK_TARGETS)
	@for bench in $(BENCHMARK_TARGETS); do \
		echo "--- Running $$bench ---"; \
		./$$bench; \
	done

sanitize: CXXFLAGS += -fsanitize=address,undefined
sanitize: LDFLAGS += -fsanitize=address,undefined
sanitize: clean test

format:
	@./run-format.sh format

check-format:
	@./run-format.sh check

TIDY_CHECKS := -*,performance-*,concurrency-*,bugprone-*,modernize-*,cppcoreguidelines-*, \
-modernize-use-trailing-return-type,-cppcoreguidelines-avoid-magic-numbers, \
-cppcoreguidelines-pro-bounds-pointer-arithmetic,-cppcoreguidelines-pro-type-reinterpret-cast, \
-cppcoreguidelines-avoid-c-arrays,-modernize-avoid-c-arrays, \
-cppcoreguidelines-pro-bounds-array-to-pointer-decay,-cppcoreguidelines-pro-type-cstyle-cast, \
-cppcoreguidelines-pro-type-vararg,-cppcoreguidelines-avoid-non-const-global-variables, \
-bugprone-throwing-static-initialization,-modernize-use-designated-initializers, \
-modernize-use-integer-sign-comparison,-modernize-use-auto,-modernize-use-ranges, \
-bugprone-narrowing-conversions,-cppcoreguidelines-narrowing-conversions, \
-modernize-avoid-c-style-cast,-cppcoreguidelines-pro-type-const-cast,-modernize-use-nodiscard, \
-modernize-use-equals-default,-cppcoreguidelines-special-member-functions, \
-cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,-performance-enum-size

TIDY_SRCS := $(filter-out %.c, $(CORE_SRCS) $(PROJECT_TEST_SRCS))

tidy: $(CORE_DEPS) $(TEST_DEPS) $(GENERATED_HPP)
	@echo "Running clang-tidy..."
	@clang-tidy -checks="$(TIDY_CHECKS)" -warnings-as-errors=* $(TIDY_SRCS) -- $(CXXFLAGS) $(INCLUDES) $(GTEST_INC)

cppcheck: $(CORE_DEPS) $(TEST_DEPS) $(GENERATED_HPP)
	@echo "Running cppcheck..."
	@mkdir -p $(BUILD_DIR)/cppcheck_build $(BUILD_DIR)/html_report
	@cppcheck $(CORE_SRCS) $(PROJECT_TEST_SRCS) \
		--cppcheck-build-dir=$(BUILD_DIR)/cppcheck_build \
		-I$(INC_DIR) -I$(INC_DIR)/libusb -I$(INC_DIR)/v4l2 -I$(LINUX_DRIVER_DIR) -I$(GEN_DIR) \
		-i$(BUILD_DIR) --suppress=*:*googletest* --suppress=*:*gmock* \
		--suppress=missingIncludeSystem \
		--suppress=variableScope:$(LINUX_DRIVER_DIR) \
		--std=c++20 --xml --enable=all --inconclusive --inline-suppr --check-level=exhaustive \
		2> $(BUILD_DIR)/cppcheck_report.xml
	@cppcheck-htmlreport --file=$(BUILD_DIR)/cppcheck_report.xml \
		--report-dir=$(BUILD_DIR)/html_report --source-dir=. --title="pi-borescope-streamer"

IWYU_SRCS    := $(CORE_SRCS) $(PROJECT_TEST_SRCS)
IWYU_TARGETS := $(addprefix iwyu-,$(IWYU_SRCS))

iwyu: $(IWYU_TARGETS)
$(IWYU_TARGETS): iwyu-%: $(CORE_DEPS) $(TEST_DEPS) $(GENERATED_HPP)
	@echo "--- Analyzing $* ---"
	@case "$*" in \
		*.c) FLAGS="$(CFLAGS)" ;; \
		*)   FLAGS="$(CXXFLAGS) $(GTEST_INC)" ;; \
	esac; \
	include-what-you-use -Xiwyu --mapping_file=project.imp \
		$$FLAGS $(INCLUDES) \
		$(if $(filter $(PLATFORM),MACOS),-isysroot $(shell xcrun --show-sdk-path) -isystem $(shell xcrun --show-sdk-path)/usr/include/c++/v1,) \
		$* || true

docs:
	@echo "Generating API Documentation with Doxygen..."
	@mkdir -p docs/api
	@echo "PROJECT_NAME = pi-borescope-streamer\nINPUT = src include\nRECURSIVE = YES\nGENERATE_HTML = YES\nGENERATE_LATEX = NO\nMACRO_EXPANSION = YES\nPREDEFINED =

define SORT_TREE_SCRIPT
import sys, re

def sort_tree(m):
    # Split lines and drop any empty ones
    lines = [l for l in m.group(1).splitlines() if l.strip()]
    # Triple check that we aren't accidentally grabbing logic or parameters
    if any("(" in l or ")" in l or "->" in l or "=" in l for l in lines):
        return m.group(0) # Abort and return completely unmodified
    lines.sort(key=len, reverse=True)
    return "\n" + "\n".join(lines) + "\n"

files = ["$(LINUX_DRIVER_DIR)/useeplus_core.c", "$(LINUX_DRIVER_DIR)/useeplus_protocol.c"]
for f in files:
    try:
        c = open(f).read()
        # Strictly match blocks where EVERY line starts with a tab/spaces, a type, and strictly ends with a semicolon
        pattern = r"\n((?:^[ \t]+(?:struct|int|char|u8|u16|u32|size_t|bool|const)\s+[^;()\n]+;\n)+)"
        c = re.sub(pattern, sort_tree, c, flags=re.M)
        open(f, "w").write(c)
    except FileNotFoundError:
        pass
endef
export SORT_TREE_SCRIPT

christmas-tree:
	@echo "Sorting local variable trees..."
	@python3 -c "$$SORT_TREE_SCRIPT"
	@echo "Applying standard formatting..."
	@clang-format -i $(LINUX_DRIVER_DIR)/useeplus_core.c $(LINUX_DRIVER_DIR)/useeplus_protocol.c
