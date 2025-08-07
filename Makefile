SHELL := /bin/bash

GPP_FLAGS = -Wall -Wextra

STATIC_LIBS_DIR = static_libs
OPENSSL_DIR = $(STATIC_LIBS_DIR)/openssl
BROTLI_DIR = $(STATIC_LIBS_DIR)/brotli

STATIC_FLAGS = -static -static-libgcc -static-libstdc++ -std=c++17
BROTLI_FLAGS = -lbrotlienc -lbrotlidec -lbrotlicommon

TARGET = bin/mercury
TARGET_WIN = bin/mercury.exe
SRCS = src/conf/*.cpp src/*.cpp src/http/*.cpp src/util/*.cpp src/io/*.cpp src/logs/*.cpp lib/*.cpp
DEPS = src/conf/*.hpp src/http/*.hpp src/util/*.hpp src/io/*.hpp src/logs/*.hpp lib/*.hpp $(SRCS)

all: $(TARGET) $(TARGET_WIN)
linux: $(TARGET)
windows: $(TARGET_WIN)

$(TARGET): $(DEPS)
	@echo -n "Building for Linux... "
	@g++ $(SRCS) -o $(TARGET) \
		$(STATIC_FLAGS) $(GPP_FLAGS) \
		-lz \
		-I$(OPENSSL_DIR)/linux/include -I$(BROTLI_DIR)/linux/include \
		-L$(OPENSSL_DIR)/linux/lib64 -L$(BROTLI_DIR)/linux/lib \
		-lssl -lcrypto \
		$(BROTLI_FLAGS) \
		2> >(grep -v -E "BIO_lookup_ex|getaddrinfo|gethostbyname")
	@upx $(TARGET) -qqq
	@echo "✅ Done."

$(TARGET_WIN): $(DEPS) src/winheader.hpp
	@echo -n "Building port for Windows... "
	@x86_64-w64-mingw32-g++-posix $(SRCS) -o $(TARGET_WIN) \
		$(STATIC_FLAGS) $(GPP_FLAGS) \
		-lz -lpthread \
		-I$(OPENSSL_DIR)/windows/include -I$(BROTLI_DIR)/windows/include \
		-L$(OPENSSL_DIR)/windows/lib64 -L$(BROTLI_DIR)/windows/lib \
		-lssl -lcrypto \
		$(BROTLI_FLAGS) \
		-lcrypt32 -lbcrypt -lws2_32
	@upx $(TARGET_WIN) -qqq
	@echo "✅ Done."

############################ STATIC BUILD CONFIG ############################

libs: static_deps static_brotli static_openssl static_zlib

static_deps:
	@./build_tools/install_deps.sh

static_brotli:
	@./build_tools/build_static_brotli.sh

static_openssl:
	@./build_tools/build_static_openssl.sh

static_zlib:
	@./build_tools/build_static_zlib.sh

release:
	@./build_tools/build_release.sh