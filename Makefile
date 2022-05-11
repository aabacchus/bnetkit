.POSIX:

XCFLAGS = $(CFLAGS) -Wall -Wextra -Wpedantic -g -Og -D_XOPEN_SOURCE=700
TLSFLAGS = $$(pkg-config --libs --cflags libtls || echo -ltls)

all: nc tlsnc

nc: nc.c
	$(CC) $(XCFLAGS) $(LDFLAGS) nc.c -o $@

tlsnc: tlsnc.c
	$(CC) $(XCFLAGS) $(LDFLAGS) $(TLSFLAGS) tlsnc.c -o $@

clean:
	rm -f nc tlsnc

.PHONY: clean
