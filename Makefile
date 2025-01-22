GPPFLAGS = -Wall -Wextra -g
TARGET = bin/main.o
TARGET_WIN = bin/main.exe
SRCS = src/*.cpp src/http/*.cpp src/util/*.cpp lib/*.cpp
DEPS = src/*.hpp src/http/*.hpp src/util/*.hpp lib/*.hpp $(SRCS)

all: $(TARGET) $(TARGET_WIN)
linux: $(TARGET)
windows: $(TARGET_WIN)

$(TARGET): $(DEPS)
	@echo -n "Building for Linux... "
	@g++ $(SRCS) -o $(TARGET) -lz $(GPPFLAGS)
	@echo "Done."

$(TARGET_WIN): $(DEPS)
	@echo -n "Building port for Windows... "
	@x86_64-w64-mingw32-g++-win32 $(SRCS) -o $(TARGET_WIN) -static -static-libgcc -static-libstdc++ -std=c++17 -lz -l ws2_32 $(GPPFLAGS)
	@echo "Done."