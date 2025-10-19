CC      ?= $(CROSS_COMPILE)gcc
CFLAGS  ?= -Wall -Wextra -Werror -O2
CFLAGS  += -std=c11 -pthread 
LDFLAGS ?=
LDLIBS  += -pthread    

TARGET  := aesdsocket
SRCS    := aesdsocket.c
OBJS    := $(SRCS:.c=.o)

ifeq ($(USE_AESD_CHAR_DEVICE),1)
    CFLAGS += -DUSE_AESD_CHAR_DEVICE=1
else
    CFLAGS += -DUSE_AESD_CHAR_DEVICE=0
endif

.PHONY: all default clean
all: $(TARGET)
default: all

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

