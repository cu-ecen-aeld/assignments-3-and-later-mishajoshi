CC ?= $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror
TARGET = writer

all: $(TARGET)

$(TARGET): writer.o
	$(CC) $(CFLAGS) writer.o -o $(TARGET)

writer.o: writer.c
	$(CC) $(CFLAGS) -c writer.c

clean:
	rm -f $(TARGET) *.o
