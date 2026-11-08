SHELL := /bin/bash

.PHONY: clean clean_build all linux windows \
	libs lib_deps libs_no_deps lib_brotli lib_openssl lib_zlib lib_pugixml lib_zstd \
	release cert

# Libraries
ARTIFACTS_LOCK := libs/artifacts.lock
OPENSSL_DIR := libs/openssl
BROTLI_DIR := libs/brotli
ZLIB_DIR := libs/zlib
ZSTD_DIR := libs/zstd
PUGIXML_DIR := libs/pugixml

INCLUDE_LINUX = $(shell find libs -type d \( -name 'include' -o -name 'pugixml' \) | grep -P '(linux/include)|(pugixml)' | sed 's/^/-I/' | tr '\n' ' ')
INCLUDE_WIN = $(shell find libs -type d \( -name 'include' -o -name 'pugixml' \) | grep -P '(windows/include)|(pugixml)' | sed 's/^/-I/' | tr '\n' ' ')
LIB_LINUX = $(shell find libs -type d -name 'lib' | grep -P 'linux/lib' | sed 's/^/-L/' | tr '\n' ' ')
LIB_WIN = $(shell find libs -type d -name 'lib' | grep -P 'windows/lib' | sed 's/^/-L/' | tr '\n' ' ')

# Compiler
CXX ?= g++
MINGW_CXX := x86_64-w64-mingw32
MINGW_CXX_SUFFIX := $(shell if command -v apt >/dev/null 2>&1; then echo g++-posix; else echo g++; fi)

# Optional compiler cache wrapper
CCACHE := $(shell command -v ccache 2>/dev/null)
ifneq ($(CCACHE),)
    ifeq ($(findstring ccache,$(CXX)),)
        CXX_CMD := $(CCACHE) $(CXX)
    else
        CXX_CMD := $(CXX)
    endif
    WIN_CXX_CMD := $(CCACHE) $(MINGW_CXX)-$(MINGW_CXX_SUFFIX)
else
    CXX_CMD := $(CXX)
    WIN_CXX_CMD := $(MINGW_CXX)-$(MINGW_CXX_SUFFIX)
endif

SRCS := $(shell find src -type f -name "*.cpp") $(wildcard $(PUGIXML_DIR)/*.cpp)

CXX_FLAGS := -Wall -Wextra -Werror
STATIC_FLAGS := -std=c++20
LIB_FLAGS := -static-libstdc++ -static-libgcc -lbrotlienc -lbrotlicommon -lz -lpthread -lssl -lcrypto -lzstd
WIN_FLAGS := -static -lcrypt32 -lws2_32 -lbcrypt -mconsole

# Targets
TARGET_LINUX := bin/mercury
TARGET_WIN := bin/mercury.exe

# Debug mode
DEBUG ?= 0
ifeq ($(DEBUG),1)
    CXX_FLAGS += -g -O0
    BUILD_MODE := debug
else
    CXX_FLAGS += -O3 -fno-strict-aliasing -ffunction-sections -fdata-sections
    LIB_FLAGS += -Wl,--gc-sections -Wl,--strip-all
    BUILD_MODE := release
endif

# Object directories
BUILD_DIR ?= build/$(BUILD_MODE)
OBJ_LINUX_DIR := $(BUILD_DIR)/obj/linux
OBJ_WIN_DIR := $(BUILD_DIR)/obj/windows
TARGET_WIN_ICON := $(OBJ_WIN_DIR)/icon.o

OBJS_LINUX := $(patsubst %.cpp, $(OBJ_LINUX_DIR)/%.o, $(SRCS))
OBJS_WIN := $(patsubst %.cpp, $(OBJ_WIN_DIR)/%.o, $(SRCS))
DEPS_LINUX := $(OBJS_LINUX:.o=.d)
DEPS_WIN := $(OBJS_WIN:.o=.d)

# Recipes
all: $(TARGET_LINUX) $(TARGET_WIN)
linux: $(TARGET_LINUX)
windows: $(TARGET_WIN)

clean:
	@./build_tools/clean.sh

clean_build:
	@rm -rf build bin/mercury bin/mercury.exe
	@echo "✅ Cleaned build artifacts."

###################################################################
############################## LINUX ##############################
###################################################################

$(OBJ_LINUX_DIR)/%.o: %.cpp $(ARTIFACTS_LOCK)
	@mkdir -p $(dir $@)
	@$(CXX_CMD) $(STATIC_FLAGS) $(CXX_FLAGS) $(INCLUDE_LINUX) -MMD -MP -c $< -o $@

$(TARGET_LINUX): $(OBJS_LINUX) $(ARTIFACTS_LOCK)
	@mkdir -p $(dir $@)
	@./build_tools/validate_libs.sh --q
	@$(CXX_CMD) \
		$(OBJS_LINUX) -o $(TARGET_LINUX) \
		$(STATIC_FLAGS) $(CXX_FLAGS) $(LIB_LINUX) $(LIB_FLAGS)
	@echo "✅ Built for Linux."

###################################################################
############################# WINDOWS #############################
###################################################################

$(TARGET_WIN_ICON): src/res/icon.rc src/res/icon.ico
	@mkdir -p $(dir $@)
	@$(MINGW_CXX)-windres $< -O coff -o $@

$(OBJ_WIN_DIR)/%.o: %.cpp $(ARTIFACTS_LOCK)
	@mkdir -p $(dir $@)
	@$(WIN_CXX_CMD) $(STATIC_FLAGS) $(CXX_FLAGS) $(INCLUDE_WIN) -MMD -MP -c $< -o $@

$(TARGET_WIN): $(OBJS_WIN) $(TARGET_WIN_ICON) src/winheader.hpp $(ARTIFACTS_LOCK)
	@if [[ ! -d ./libs/brotli/windows ]] || [[ ! -d ./libs/openssl/windows ]] || [[ ! -d ./libs/zlib/windows ]] || [[ ! -d ./libs/zstd/windows ]]; then \
		echo "Missing Windows libraries"; \
		exit 1; \
	fi
	@./build_tools/validate_libs.sh --q
	@mkdir -p $(dir $@)
	@$(WIN_CXX_CMD) \
		$(OBJS_WIN) $(TARGET_WIN_ICON) -o $(TARGET_WIN) \
		$(STATIC_FLAGS) $(CXX_FLAGS) $(LIB_WIN) $(LIB_FLAGS) $(WIN_FLAGS)
	@echo "✅ Built for Windows."

ifeq ($(filter clean%,$(MAKECMDGOALS)),)
    -include $(DEPS_LINUX)
    -include $(DEPS_WIN)
endif

###################################################################
############################ LIBRARIES ############################
###################################################################

libs: lib_deps libs_no_deps
libs_no_deps: lib_brotli lib_openssl lib_zlib lib_pugixml lib_zstd

lib_deps:
	@./build_tools/install_deps.sh

lib_brotli:
	@./build_tools/lib_brotli.sh

lib_openssl:
	@./build_tools/lib_openssl.sh

lib_zlib:
	@./build_tools/lib_zlib.sh

lib_pugixml:
	@./build_tools/lib_pugixml.sh

lib_zstd:
	@./build_tools/lib_zstd.sh

release:
	@./build_tools/validate_libs.sh --q
	@./build_tools/release.sh

# Runs tests against various Docker images
docker_tests:
	@./docker/run_tests.sh

###################################################################
############################ TLS CERTS ############################
###################################################################

# Create a 365-day self-signed TLS 1.3 cert
cert:
	@mkdir -p conf/ssl
	@openssl req -x509 \
		-newkey rsa:4096 \
		-keyout conf/ssl/key.pem \
		-out conf/ssl/cert.pem \
		-sha256 \
		-days 365 \
		-nodes
