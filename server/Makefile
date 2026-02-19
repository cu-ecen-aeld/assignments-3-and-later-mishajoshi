# server/Makefile

CC ?= gcc
CFLAGS = -Wall -Werror
TARGET = aesdsocket

all: $(TARGET)

$(TARGET): aesdsocket.o
	$(CC) $(CFLAGS) -o $(TARGET) aesdsocket.o

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o $(TARGET)
