SHELL := /bin/bash

GPP_FLAGS = -Wall -Wextra -Winvalid-pch

LIBS_DIR = libs
OPENSSL_DIR = $(LIBS_DIR)/openssl
BROTLI_DIR = $(LIBS_DIR)/brotli
ZSTD_DIR = $(LIBS_DIR)/zstd
PUGIXML_DIR = $(LIBS_DIR)/pugixml
ARTIFACTS_LOCK = $(LIBS_DIR)/artifacts.lock
PCH_DIR = src/pch

STATIC_FLAGS = -static -static-libgcc -static-libstdc++ -std=c++20
BROTLI_FLAGS = -lbrotlienc -lbrotlidec -lbrotlicommon
LIB_FLAGS = -lz -lpthread -lssl -lcrypto -lzstd

TARGET = bin/mercury
TARGET_WIN = bin/mercury.exe
TARGET_WIN_ICON = bin/icon.o
SRCS = src/conf/*.cpp \
	src/*.cpp \
	src/http/*.cpp src/http/version/*.cpp src/http/cgi/*.cpp \
	src/util/*.cpp \
	src/io/*.cpp \
	$(PUGIXML_DIR)/*.cpp \
	src/logs/*.cpp
DEPS = src/conf/*.hpp \
	src/http/*.hpp src/http/version/*.hpp src/http/cgi/*.hpp \
	src/util/*.hpp \
	src/io/*.hpp \
	src/logs/*.hpp \
	$(PUGIXML_DIR)/*.hpp \
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
		-I$(OPENSSL_DIR)/linux/include -I$(BROTLI_DIR)/linux/include -I$(ZSTD_DIR)/linux/include -I$(PUGIXML_DIR) \
		-L$(OPENSSL_DIR)/linux/lib -L$(BROTLI_DIR)/linux/lib -L$(ZSTD_DIR)/linux/lib \
		$(LIB_FLAGS) \
		$(BROTLI_FLAGS) \
		2> >(grep -v -E "BIO_lookup_ex|getaddrinfo|gethostbyname|fetchLatestVersion")
	@upx $(TARGET) -qqq
	@echo "✅ Done."

$(TARGET_WIN): $(DEPS) src/winheader.hpp src/res/icon.ico $(ARTIFACTS_LOCK)
	@./build_tools/validate_libs.sh --q
	@echo -n "Building for Windows... "
	@./build_tools/init_conf.sh
	@x86_64-w64-mingw32-windres src/res/icon.rc -O coff -o $(TARGET_WIN_ICON)
	@x86_64-w64-mingw32-g++-posix \
		-include $(PCH_DIR)/common-win.hpp $(PCH_DIR)/common.hpp \
		$(SRCS) $(TARGET_WIN_ICON) -o $(TARGET_WIN) \
		$(STATIC_FLAGS) $(GPP_FLAGS) \
		-I$(OPENSSL_DIR)/windows/include -I$(BROTLI_DIR)/windows/include -I$(ZSTD_DIR)/windows/include -I$(PUGIXML_DIR) \
		-L$(OPENSSL_DIR)/windows/lib -L$(BROTLI_DIR)/windows/lib -L$(ZSTD_DIR)/windows/lib \
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
		-I$(OPENSSL_DIR)/linux/include -I$(BROTLI_DIR)/linux/include -I$(ZSTD_DIR)/linux/include -I$(PUGIXML_DIR) \
		-L$(OPENSSL_DIR)/linux/lib -L$(BROTLI_DIR)/linux/lib -L$(ZSTD_DIR)/linux/lib \
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
		-I$(OPENSSL_DIR)/windows/include -I$(BROTLI_DIR)/windows/include -I$(ZSTD_DIR)/windows/include -I$(PUGIXML_DIR) \
		-L$(OPENSSL_DIR)/windows/lib -L$(BROTLI_DIR)/windows/lib -L$(ZSTD_DIR)/windows/lib \
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

libs: lib_deps lib_brotli lib_openssl lib_zlib lib_pugixml lib_zstd

lib_deps:
	@./build_tools/install_deps.sh

lib_brotli:
	@./build_tools/build_lib_brotli.sh

lib_openssl:
	@./build_tools/build_lib_openssl.sh

lib_zlib:
	@./build_tools/build_lib_zlib.sh

lib_pugixml:
	@./build_tools/fetch_lib_pugixml.sh

lib_zstd:
	@./build_tools/build_lib_zstd.sh

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