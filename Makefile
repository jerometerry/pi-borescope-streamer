.PHONY: all clean useeplus

CXX := zig c++

ifneq (,$(shell command -v ccache 2>/dev/null))
    CXX := ccache $(CXX)
endif

CXXFLAGS := -std=c++2b -Wall -Wextra -g -fno-omit-frame-pointer -Wno-deprecated-declarations

SRC_DIR   := src
BUILD_DIR := build

# --- Library Paths ---
USEEPLUS_DIR := ../useeplus
USEEPLUS_INC := $(USEEPLUS_DIR)/include/useeplus
USEEPLUS_LIB := $(USEEPLUS_DIR)/build/libuseeplus.a

USOCKETS_LIB := $(USEEPLUS_DIR)/build/sockets/uSockets/uSockets.a
USOCKETS_INC := $(USEEPLUS_DIR)/build/sockets/uSockets/src
UWEBSOCKETS_INC := $(USEEPLUS_DIR)/build/sockets/uWebSockets/src

UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -p)

# Linker flags pointing to our new static library
LDFLAGS := -Wl,--start-group $(USOCKETS_LIB) $(USEEPLUS_LIB) -Wl,--end-group -lz -lssl -lcrypto -lpthread

ifeq ($(UNAME_S),Linux)
    PLATFORM := LINUX
    CXXFLAGS += -pthread
    ifneq (,$(filter aarch64%,$(UNAME_P)))
        CXXFLAGS += -target aarch64-linux-gnu -mcpu=cortex-a76
    endif
else ifeq ($(UNAME_S),Darwin)
    PLATFORM := MACOS
endif

LIBUSB_CFLAGS := $(shell pkg-config --cflags libusb-1.0)
LIBUSB_LIBS   := $(shell pkg-config --libs libusb-1.0)

LDFLAGS += $(LIBUSB_LIBS)

INCLUDES := -I$(USEEPLUS_INC) \
            -isystem $(UWEBSOCKETS_INC) \
            -isystem $(USOCKETS_INC) \
            $(patsubst -I%,-isystem %,$(LIBUSB_CFLAGS))

ifeq ($(PLATFORM),MACOS)
    OSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null || echo "-I/opt/homebrew/opt/openssl@3/include")
    INCLUDES += $(patsubst -I%,-isystem %,$(OSSL_CFLAGS))
    LDFLAGS  += $(shell pkg-config --libs-only-L openssl 2>/dev/null || echo "-L/opt/homebrew/opt/openssl@3/lib")
endif

# --- Application Targets ---
LIBUSB_APP_SRC := $(SRC_DIR)/libusb/mjpeg_server_main.cpp
V4L2_APP_SRC   := $(SRC_DIR)/v4l2/v4l2_mjpeg_server_main.cpp

LIBUSB_APP := $(BUILD_DIR)/mjpeg_server
V4L2_APP   := $(BUILD_DIR)/v4l2_mjpeg_server

# Typing `make` will automatically build the library first, then the two servers
all: useeplus $(LIBUSB_APP) $(V4L2_APP)

useeplus:
	@echo "Ensuring useeplus core library is built..."
	@$(MAKE) -C $(USEEPLUS_DIR)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(LIBUSB_APP): $(LIBUSB_APP_SRC) $(USEEPLUS_LIB) | $(BUILD_DIR)
	@echo "Building libusb MJPEG server..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LDFLAGS)

$(V4L2_APP): $(V4L2_APP_SRC) $(USEEPLUS_LIB) | $(BUILD_DIR)
	@echo "Building V4L2 MJPEG server..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
