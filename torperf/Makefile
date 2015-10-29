### Copyright 2007 Steven J. Murdoch
### update 2015 Kelly Kaoudis
### See LICENSE for licensing information

CC=clang -Wall -Werror -ggdb

all: trivsocks

trivsocks: trivsocks.o util.o
	$(CC) -o $@ $^

%.o: %.c
	$(CC) -c $<

test: trivsocks
	./trivsocks -4 tor.eff.org /
	./trivsocks -5 tor.eff.org /

clean:
	rm -f *~ *.o trivsocks
