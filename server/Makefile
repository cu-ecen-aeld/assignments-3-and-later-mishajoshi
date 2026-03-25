CC ?= gcc

CFLAGS += -Wall -Werror
LDFLAGS += $(LDFLAGS)

TARGET = aesdsocket
OBJS = aesdsocket.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET)
