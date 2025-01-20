GPPFLAGS = -Wall -Wextra -g
TARGET = main.o

$(TARGET): *.hpp *.cpp
	@g++ *.cpp -o $(TARGET) -lz $(GPPFLAGS)