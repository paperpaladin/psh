CC      = gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2
LDFLAGS ?=

all: psh

psh: psh.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: psh
	./t/run.sh

# The oracle under sanitizers. Leak detection is off on purpose: psh never
# frees its parse trees (see the shortcut note at the top of psh.c), so
# LeakSanitizer would report hundreds of by-design leaks and bury the real
# findings. ASAN/UBSAN still catch the bugs that matter here -- overruns in the
# expansion buffers and bad arithmetic on the quote-flag arrays.
test-asan: psh-asan
	ASAN_OPTIONS=detect_leaks=0 PSH=./psh-asan ./t/run.sh

psh-asan: psh.c
	$(CC) -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer -o $@ $<

clean:
	rm -f psh psh-asan

.PHONY: all test test-asan clean
