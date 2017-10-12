.PHONY: all clean depend

CFLAGS := -Wall -Werror  -fPIC
LDFLAGS := -lpthread -lleveldb -lsnappy
CC := g++

SRCS := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp,%.o,$(SRCS))


TARGET := ldbt

all: $(TARGET)

$(TARGET): $(OBJS)
		$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o:%.c
		$(CC) -c $(CFLAGS) $< -o $@

explain:
		@echo "The information represents in the program:"
		@echo "Final executable name: $(PRGM)"
		@echo "Source files: $(SRCS)"
		@echo "Object files: $(OBJS)"

depend:$(DEPS)
		@echo "Dependencies are now up-to-date"

clean:
		rm -f $(TARGET) $(OBJS) $(DEPS)

-include $(DEPS) 
