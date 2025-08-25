SHELL := /bin/bash

GPP_FLAGS = -Wall -Wextra -Winvalid-pch

STATIC_LIBS_DIR = static_libs
OPENSSL_DIR = $(STATIC_LIBS_DIR)/openssl
BROTLI_DIR = $(STATIC_LIBS_DIR)/brotli
ARTIFACTS_LOCK = $(STATIC_LIBS_DIR)/artifacts.lock
PCH_DIR = src/pch

STATIC_FLAGS = -static -static-libgcc -static-libstdc++ -std=c++20
BROTLI_FLAGS = -lbrotlienc -lbrotlidec -lbrotlicommon
LIB_FLAGS = -lz -lpthread -lssl -lcrypto

TARGET = bin/mercury
TARGET_WIN = bin/mercury.exe
SRCS = src/conf/*.cpp \
	src/*.cpp \
	src/http/*.cpp src/http/version/*.cpp \
	src/util/*.cpp \
	src/io/*.cpp \
	src/logs/*.cpp \
	lib/*.cpp
DEPS = src/conf/*.hpp \
	src/http/*.hpp src/http/version/*.hpp \
	src/util/*.hpp \
	src/io/*.hpp \
	src/logs/*.hpp \
	lib/*.hpp \
	src/pch/*.hpp \
	$(SRCS)

all: pch_linux $(TARGET) pch_windows $(TARGET_WIN)
linux: pch_linux $(TARGET)
windows: pch_windows $(TARGET_WIN)

$(TARGET): $(DEPS) $(ARTIFACTS_LOCK)
	@./build_tools/validate_libs.sh --q
	@echo -n "Building for Linux... "
	@./build_tools/init_conf.sh
	@g++ \
		-include $(PCH_DIR)/common-linux.hpp $(PCH_DIR)/common.hpp \
		$(SRCS) -o $(TARGET) \
		$(STATIC_FLAGS) $(GPP_FLAGS) \
		-I$(OPENSSL_DIR)/linux/include -I$(BROTLI_DIR)/linux/include \
		-L$(OPENSSL_DIR)/linux/lib64 -L$(BROTLI_DIR)/linux/lib \
		$(LIB_FLAGS) \
		$(BROTLI_FLAGS) \
		2> >(grep -v -E "BIO_lookup_ex|getaddrinfo|gethostbyname|fetchLatestVersion")
	@upx $(TARGET) -qqq
	@echo "✅ Done."

$(TARGET_WIN): $(DEPS) src/winheader.hpp $(ARTIFACTS_LOCK)
	@./build_tools/validate_libs.sh --q
	@echo -n "Building for Windows... "
	@./build_tools/init_conf.sh
	@x86_64-w64-mingw32-g++-posix \
		-include $(PCH_DIR)/common-win.hpp $(PCH_DIR)/common.hpp \
		$(SRCS) -o $(TARGET_WIN) \
		$(STATIC_FLAGS) $(GPP_FLAGS) \
		-I$(OPENSSL_DIR)/windows/include -I$(BROTLI_DIR)/windows/include \
		-L$(OPENSSL_DIR)/windows/lib64 -L$(BROTLI_DIR)/windows/lib \
		$(LIB_FLAGS) \
		$(BROTLI_FLAGS) \
		-lcrypt32 -lbcrypt -lws2_32 \
		-Wl,--subsystem,console
	@upx $(TARGET_WIN) -qqq
	@echo "✅ Done."

# Precompiled headers
pch: pch_linux pch_windows
pch_linux: $(PCH_DIR)/common-linux.hpp.gch
pch_windows: $(PCH_DIR)/common-win.hpp.gch

$(PCH_DIR)/common-linux.hpp.gch: $(PCH_DIR)/common-linux.hpp $(PCH_DIR)/common.hpp $(ARTIFACTS_LOCK)
	@./build_tools/validate_libs.sh --q
	@./build_tools/init_conf.sh
	@echo -n "Building Linux PCH... "
	@g++ -x c++-header \
		$(STATIC_FLAGS) $(GPP_FLAGS) \
		-I$(OPENSSL_DIR)/linux/include -I$(BROTLI_DIR)/linux/include \
		-L$(OPENSSL_DIR)/linux/lib64 -L$(BROTLI_DIR)/linux/lib \
		$(LIB_FLAGS) \
		$(BROTLI_FLAGS) \
		-c $(PCH_DIR)/common.hpp \
		-o $(PCH_DIR)/common-linux.hpp.gch
	@echo "✅ Done."

$(PCH_DIR)/common-win.hpp.gch: $(PCH_DIR)/common-win.hpp $(PCH_DIR)/common.hpp $(ARTIFACTS_LOCK)
	@./build_tools/validate_libs.sh --q
	@./build_tools/init_conf.sh
	@echo -n "Building Windows PCH... "
	@x86_64-w64-mingw32-g++-posix -x c++-header \
		$(STATIC_FLAGS) $(GPP_FLAGS) \
		-I$(OPENSSL_DIR)/windows/include -I$(BROTLI_DIR)/windows/include \
		-L$(OPENSSL_DIR)/windows/lib64 -L$(BROTLI_DIR)/windows/lib \
		$(LIB_FLAGS) \
		$(BROTLI_FLAGS) \
		-c $(PCH_DIR)/common.hpp \
		-o $(PCH_DIR)/common-win.hpp.gch
	@echo "✅ Done."

############################ STATIC BUILD CONFIG ############################

# Called if the artifacts.lock file (which is a dependency for some recipes) doesn't exist
$(ARTIFACTS_LOCK):
	@if [ ! -e $(ARTIFACTS_LOCK) ]; then \
		echo "" | gzip | base64 > $(ARTIFACTS_LOCK); \
	fi

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
	@./build_tools/validate_libs.sh --q
	@./build_tools/build_release.sh

############################ TLS CERTS ############################

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