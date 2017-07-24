KERNEL_SOURCE := /lib/modules/$(shell uname -r)/source/usr

CPPFLAGS := -I$(KERNEL_SOURCE)/include -D_GNU_SOURCE
CFLAGS := -g -Wall -W -Wextra -O2
LDFLAGS := $(CFLAGS)

TARGET := worm
all: $(TARGET)


SRCS := $(wildcard *.c)
DEPS := $(patsubst %.c,%.d,$(SRCS))
OBJS := $(patsubst %.c,%.o,$(SRCS))

$(TARGET): $(OBJS) $(DEPS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.d:%.c
	$(CC) -M $(CFLAGS) $(CPPFLAGS) $< > $@

%.o:%.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

clean:
	rm -f $(TARGET) ${TARGET}.o $(TARGET).d
