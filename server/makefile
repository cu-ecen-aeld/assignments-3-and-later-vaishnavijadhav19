CC      ?= $(CROSS_COMPILE)gcc
CFLAGS  ?= -Wall -Wextra -Werror -O2
LDFLAGS ?=

TARGET  := aesdsocket
SRCS    := aesdsocket.c
OBJS    := $(SRCS:.c=.o)

.PHONY: all default clean
all: $(TARGET)
default: all

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

