SHELL := /bin/bash

.PHONY: clean all linux windows \
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

SRCS := $(shell find src -type f -name "*.cpp") $(wildcard $(PUGIXML_DIR)/*.cpp)
DEPS := $(shell find src -type f -name "*.hpp") $(wildcard $(PUGIXML_DIR)/*.hpp) $(SRCS)

CXX_FLAGS := -Wall -Wextra -Werror
STATIC_FLAGS := -std=c++20
LIB_FLAGS := -static-libstdc++ -static-libgcc -lbrotlienc -lbrotlicommon -lz -lpthread -lssl -lcrypto -lzstd
WIN_FLAGS := -static -lcrypt32 -lws2_32 -mconsole

# Targets
TARGET_LINUX := bin/mercury
TARGET_WIN := bin/mercury.exe
TARGET_WIN_ICON := bin/icon.o

# Recipes
all: $(TARGET_LINUX) $(TARGET_WIN)
linux: $(TARGET_LINUX)
windows: $(TARGET_WIN)

clean:
	@./build_tools/clean.sh

###################################################################
############################## LINUX ##############################
###################################################################

$(TARGET_LINUX): $(DEPS) $(ARTIFACTS_LOCK)
	@./build_tools/validate_libs.sh --q
	@$(CXX) \
		$(SRCS) -o $(TARGET_LINUX) \
		$(STATIC_FLAGS) $(CXX_FLAGS) $(INCLUDE_LINUX) $(LIB_LINUX) $(LIB_FLAGS)
	@upx $(TARGET_LINUX) -qqq
	@echo "✅ Built for Linux."

###################################################################
############################# WINDOWS #############################
###################################################################

$(TARGET_WIN): $(DEPS) src/winheader.hpp src/res/icon.ico $(ARTIFACTS_LOCK)
	@if [[ ! -d ./libs/brotli/windows ]] || [[ ! -d ./libs/openssl/windows ]] || [[ ! -d ./libs/zlib/windows ]] || [[ ! -d ./libs/zstd/windows ]]; then \
		echo "Missing Windows libraries"; \
		exit 1; \
	fi
	@./build_tools/validate_libs.sh --q
	@$(MINGW_CXX)-windres src/res/icon.rc -O coff -o $(TARGET_WIN_ICON)
	@$(MINGW_CXX)-$(MINGW_CXX_SUFFIX) \
		$(SRCS) $(TARGET_WIN_ICON) -o $(TARGET_WIN) \
		$(STATIC_FLAGS) $(CXX_FLAGS) $(INCLUDE_WIN) $(LIB_WIN) $(LIB_FLAGS) $(WIN_FLAGS)
	@upx $(TARGET_WIN) -qqq
	@echo "✅ Built for Windows."

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
