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

UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -p)

ifeq ($(UNAME_S),Linux)
    PLATFORM := LINUX
    CXXFLAGS += -pthread
    LDFLAGS  += -pthread
    ifneq (,$(filter aarch64%,$(UNAME_P)))
        CXXFLAGS += -mcpu=cortex-a76 -mtune=cortex-a76
        CFLAGS   += -mcpu=cortex-a76 -mtune=cortex-a76
    endif
else ifeq ($(UNAME_S),Darwin)
    PLATFORM := MACOS
endif

INCLUDES := -I$(INC_DIR) \
            -I$(INC_DIR)/libusb \
            -I$(INC_DIR)/v4l2 \
            -I$(LINUX_DRIVER_DIR) \
            -I$(GEN_DIR) \
            -Ithird_party/uWebSockets/src \
            -Ithird_party/uSockets/src

LDFLAGS += -Wl,--start-group third_party/uSockets/uSockets.a build/libmjpeg_streamer_core.a \
	-Wl,--end-group -lz -lssl -lcrypto -lpthread

ifeq ($(PLATFORM),MACOS)
    INCLUDES += $(shell pkg-config --cflags libusb-1.0)
    LDFLAGS  += $(shell pkg-config --libs libusb-1.0)

    INCLUDES += $(shell pkg-config --cflags openssl 2>/dev/null || echo "-I/opt/homebrew/opt/openssl@3/include")
    LDFLAGS  += $(shell pkg-config --libs-only-L openssl 2>/dev/null || echo "-L/opt/homebrew/opt/openssl@3/lib")
endif

HTML_FILE     := $(SRC_DIR)/index.html
GENERATED_HPP := $(GEN_DIR)/index_html.hpp

CORE_SRCS := $(SRC_DIR)/mjpeg_server.cpp \
             $(SRC_DIR)/zero_allocation_response_builder.cpp

ifeq ($(PLATFORM),MACOS)
    CORE_SRCS += $(SRC_DIR)/libusb/mjpeg_stream_capture.cpp \
                 $(SRC_DIR)/libusb/usb_camera.cpp \
                 $(SRC_DIR)/libusb/usb_context.cpp \
                 $(SRC_DIR)/libusb/usb_device_finder.cpp \
                 $(SRC_DIR)/libusb/usb_driver.cpp \
                 $(SRC_DIR)/libusb/mjpeg_stream.cpp \
                 $(LINUX_DRIVER_DIR)/useeplus_protocol.c
endif

CORE_OBJS := $(patsubst %,$(BUILD_DIR)/%.o,$(basename $(CORE_SRCS)))
CORE_LIB  := $(BUILD_DIR)/libmjpeg_streamer_core.a

ifeq ($(PLATFORM),LINUX)
    TARGETS := $(BUILD_DIR)/v4l2_mjpeg_server
else ifeq ($(PLATFORM),MACOS)
    TARGETS := $(BUILD_DIR)/mjpeg_server \
               $(BUILD_DIR)/mjpeg_stream_capture \
               $(BUILD_DIR)/mjpeg_frame_extractor
endif

.PHONY: all clean format check-format

all: $(TARGETS)

$(GENERATED_HPP): $(HTML_FILE) GenerateHeader.cmake
	@mkdir -p $(GEN_DIR)
	@echo "Converting $< to C++ header format"
	@cmake -D INPUT_HTML="$(HTML_FILE)" -D OUTPUT_HPP="$(GENERATED_HPP)" -P GenerateHeader.cmake

$(BUILD_DIR)/%.o: %.cpp $(GENERATED_HPP)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: %.c $(GENERATED_HPP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(CORE_LIB): $(CORE_OBJS)
	@echo "Archiving $@"
	@ar rcs $@ $^

$(BUILD_DIR)/v4l2_mjpeg_server: $(BUILD_DIR)/src/v4l2/v4l2_mjpeg_stream.o $(BUILD_DIR)/src/v4l2/v4l2_mjpeg_server_main.o $(CORE_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/mjpeg_server: $(BUILD_DIR)/src/libusb/mjpeg_server_main.o $(CORE_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/mjpeg_stream_capture: $(BUILD_DIR)/src/libusb/mjpeg_stream_capture_main.o $(CORE_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/mjpeg_frame_extractor: $(BUILD_DIR)/src/libusb/mjpeg_frame_extractor_main.o $(CORE_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR) $(GEN_DIR)
