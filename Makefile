CC=gcc
CFLAGS=-g -Wall -D_FILE_OFFSET_BITS=64
LDFLAGS=-lfuse

OBJ=tfs.o block.o

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

tfs: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o tfs

link: 
	./tfs -s -d  /tmp/lhs52/mountdir

unlink:
	fusermount -u /tmp/lhs52/mountdir

simple:
	./tfs -simple



.PHONY: clean
clean:
	rm -f *.o tfs DISKFILE



