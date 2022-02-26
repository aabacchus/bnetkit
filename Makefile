.POSIX:

XCFLAGS = $(CFLAGS) -Wall -Wextra -Wpedantic -g -Og -D_XOPEN_SOURCE=700

nc: nc.c
	$(CC) $(XCFLAGS) $(LDFLAGS) nc.c -o $@

clean:
	rm -f nc
