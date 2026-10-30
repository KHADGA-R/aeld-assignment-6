
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -Werror -g -pthread

TARGET = aesdsocket
SOURCES = aesdsocket.c

.PHONY: all default clean

all: $(TARGET)

default: all

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET) *.o
