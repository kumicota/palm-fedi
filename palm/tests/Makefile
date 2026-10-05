# Host-side unit tests for the platform-independent C code.
CFLAGS = -Wall -Wno-multichar -g -fsanitize=address,undefined -Ishim -I../src

test: test_proto
	./test_proto

# net.c's URL helpers live at the bottom of the file; extract them so the
# NetLib parts don't need to compile on the host.
url_helpers.c: ../src/net.c
	(echo '#include "PalmFedi.h"'; sed -n '/^void UrlInit/,$$p' ../src/net.c) > $@

test_proto: test_proto.c ../src/proto.c url_helpers.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f test_proto url_helpers.c
.PHONY: test clean
