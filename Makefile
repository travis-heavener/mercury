GPPFLAGS = -Wall -Wextra -g
TARGET = main.o
SRCS = *.cpp http/*.cpp
DEPS = */*.hpp $(SRCS)

$(TARGET): $(DEPS)
	@g++ $(SRCS) -o $(TARGET) -lz $(GPPFLAGS)