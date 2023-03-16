BIN:=devctl
SRCS:=$(wildcard *.c)
HEADERS:=$(wildcard *.h)
OBJS:=$(SRCS:.c=.o)
CFLAGS:=-std=gnu11 -Wall -Wextra -Wconversion -Wmissing-prototypes \
-Wstrict-prototypes -Wunused-parameter -Wuninitialized -Wshadow \
-Wbad-function-cast -Wcast-qual -Wdouble-promotion -Wformat=2 \
-Wformat-overflow=2 -Wformat-signedness -Wformat-truncation=2 \
-Wnull-dereference -Winit-self -Wmissing-include-dirs -Wswitch-default \
-fhonour-copts
LDLIBS:=-lubus -lubox -lblobmsg_json -luci -lserialport

.PHONY: all
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

# Creates a separate rule for each .o file
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

.PHONY: format
format:
	clang-format -i --style=file --verbose $(SRCS) $(HEADERS)

.PHONY: cppcheck
cppcheck:
	cppcheck --enable=all .
