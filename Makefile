SHELL := /bin/bash

.PHONY: clean all linux windows pch pch_linux pch_windows \
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
CXX := g++
MINGW_CXX := x86_64-w64-mingw32

SRCS := $(shell find src -type f -name "*.cpp") $(wildcard $(PUGIXML_DIR)/*.cpp)
DEPS := $(shell find src -type f -name "*.hpp") $(wildcard $(PUGIXML_DIR)/*.hpp) $(SRCS)

PCH_DIR := src/pch
PCH_WIN := $(PCH_DIR)/common-win.hpp $(PCH_DIR)/common.hpp
PCH_LINUX := $(PCH_DIR)/common-linux.hpp $(PCH_DIR)/common.hpp

CXX_FLAGS := -Wall -Wextra -Winvalid-pch
STATIC_FLAGS := -static -std=c++20
LIB_FLAGS := -lbrotlienc -lbrotlicommon -lz -lpthread -lssl -lcrypto -lzstd
WIN_FLAGS := -lcrypt32 -lws2_32 -mconsole

# Targets
TARGET_LINUX := bin/mercury
TARGET_WIN := bin/mercury.exe
TARGET_WIN_ICON := bin/icon.o

# Recipes
all: $(TARGET_LINUX) $(TARGET_WIN)
linux: $(TARGET_LINUX)
windows: $(TARGET_WIN)

pch: pch_linux pch_windows
pch_linux: $(PCH_DIR)/common-linux.hpp.gch
pch_windows: $(PCH_DIR)/common-win.hpp.gch

clean:
	@rm -f bin/* tmp/* releases/* conf/ssl/*.pem
	@rm -rf libs
	@cp -f conf/default/* conf
	@mkdir -p logs bin libs
	@find . -type f -name "*.sh" -exec chmod +x {} \;
	@find ./build_tools -type f -not -name "*.sh" -exec sh -c 'head -n 1 "$1" | grep -q "^#!" && chmod +x "$1"' _ {} \;
	@echo -n "" > logs/access.log
	@echo -n "" > logs/error.log
	@echo -n "" > $(ARTIFACTS_LOCK)

###################################################################
############################## LINUX ##############################
###################################################################

$(TARGET_LINUX): $(DEPS) $(ARTIFACTS_LOCK)
	@make pch_linux --no-print-directory -s
	@./build_tools/validate_libs.sh --q
	@$(CXX) -include $(PCH_LINUX) \
		$(SRCS) -o $(TARGET_LINUX) \
		$(STATIC_FLAGS) $(CXX_FLAGS) $(INCLUDE_LINUX) $(LIB_LINUX) $(LIB_FLAGS) \
		2> >(grep -v -E "BIO_lookup_ex|getaddrinfo|gethostbyname|fetchLatestVersion")
	@upx $(TARGET_LINUX) -qqq
	@echo "✅ Built for Linux."

$(PCH_DIR)/common-linux.hpp.gch: $(PCH_LINUX) $(ARTIFACTS_LOCK)
	@./build_tools/validate_libs.sh --q
	@$(CXX) -x c++-header \
		$(STATIC_FLAGS) $(CXX_FLAGS) $(INCLUDE_LINUX) $(LIB_LINUX) $(LIB_FLAGS) \
		-c $(PCH_DIR)/common.hpp -o $(PCH_DIR)/common-linux.hpp.gch
	@echo "✅ Built Linux PCH."

###################################################################
############################# WINDOWS #############################
###################################################################

$(TARGET_WIN): $(DEPS) src/winheader.hpp src/res/icon.ico $(ARTIFACTS_LOCK)
	@make pch_windows --no-print-directory -s
	@./build_tools/validate_libs.sh --q
	@$(MINGW_CXX)-windres src/res/icon.rc -O coff -o $(TARGET_WIN_ICON)
	@$(MINGW_CXX)-g++-posix -include $(PCH_WIN) \
		$(SRCS) $(TARGET_WIN_ICON) -o $(TARGET_WIN) \
		$(STATIC_FLAGS) $(CXX_FLAGS) $(INCLUDE_WIN) $(LIB_WIN) $(LIB_FLAGS) $(WIN_FLAGS)
	@upx $(TARGET_WIN) -qqq
	@echo "✅ Built for Windows."

$(PCH_DIR)/common-win.hpp.gch: $(PCH_WIN) $(ARTIFACTS_LOCK)
	@./build_tools/validate_libs.sh --q
	@$(MINGW_CXX)-g++-posix -x c++-header \
		$(STATIC_FLAGS) $(CXX_FLAGS) $(INCLUDE_WIN) $(LIB_WIN) $(LIB_FLAGS) $(WIN_FLAGS) \
		-c $(PCH_DIR)/common.hpp -o $(PCH_DIR)/common-win.hpp.gch
	@echo "✅ Built Windows PCH."

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