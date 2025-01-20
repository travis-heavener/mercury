GPPFLAGS = -Wall -Wextra -g
TARGET = main.o
TARGET_WIN = main.exe
SRCS = *.cpp http/*.cpp
DEPS = */*.hpp $(SRCS)

all: $(TARGET) $(TARGET_WIN)
unix: $(TARGET)
windows: $(TARGET_WIN)

$(TARGET): $(DEPS)
	@echo -n "Building for Linux... "
	@g++ $(SRCS) -o $(TARGET) -lz $(GPPFLAGS)
	@echo "Done."

$(TARGET_WIN): $(DEPS)
	@echo -n "Building port for Windows... "
	@x86_64-w64-mingw32-g++-win32 $(SRCS) -o $(TARGET_WIN) -static -static-libgcc -static-libstdc++ -std=c++17 -l ws2_32 $(GPPFLAGS)
	@echo "Done."