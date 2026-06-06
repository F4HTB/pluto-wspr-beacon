CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra -std=c11
CPPFLAGS += -Iinclude
LDLIBS += -lm

DSP_SRCS=src/wspr_encode.c src/nco.c
TX_SRCS=$(DSP_SRCS) src/wspr_tx.c

.PHONY: all clean check
all: wspr-beacon

wspr-beacon: $(TX_SRCS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(TX_SRCS) $(LDLIBS) -liio

check:

clean:
	rm -f wspr-beacon *.o
