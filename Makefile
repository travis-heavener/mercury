GPP_FLAGS = -Wall -Wextra
OPENSSL_DIR = $(HOME)/openssl-static
OPENSSL_WIN_DIR = $(OPENSSL_DIR)/windows

STATIC_FLAGS = -static -static-libgcc -static-libstdc++ -std=c++17

TARGET = bin/main.o
TARGET_WIN = bin/main.exe
SRCS = src/*.cpp src/http/*.cpp src/util/*.cpp lib/*.cpp
DEPS = src/*.hpp src/http/*.hpp src/util/*.hpp lib/*.hpp $(SRCS)

all: $(TARGET) $(TARGET_WIN)
linux: $(TARGET)
windows: $(TARGET_WIN)

$(TARGET): $(DEPS)
	@echo -n "Building for Linux... "
	@g++ $(SRCS) -o $(TARGET) \
		$(STATIC_FLAGS) $(GPP_FLAGS) \
		-lz -I$(OPENSSL_DIR)/include \
		-L$(OPENSSL_DIR)/lib64 -lssl -lcrypto \
		-L/usr/lib/x86_64-linux-gnu -lbrotlienc -lbrotlidec -lbrotlicommon
	@upx $(TARGET) -qqq
	@echo "✅ Done."

$(TARGET_WIN): $(DEPS)
	@echo -n "Building port for Windows... "
	@x86_64-w64-mingw32-g++-posix $(SRCS) -o $(TARGET_WIN) \
		$(STATIC_FLAGS) $(GPP_FLAGS) \
		-lz -lpthread -I$(OPENSSL_WIN_DIR)/include \
		-L$(OPENSSL_WIN_DIR)/lib64 -lssl -lcrypto -lcrypt32 -lbcrypt -lws2_32 
	@upx $(TARGET_WIN) -qqq
	@echo "✅ Done."