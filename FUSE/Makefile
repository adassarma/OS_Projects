BINS = mount.wfs mkfs.wfs fsck.wfs

CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18
FUSE_CFLAGS = `pkg-config fuse --cflags --libs`

.PHONY: all
all: $(BINS)

mount.wfs:
	$(CC) $(CFLAGS) mount.wfs.c $(FUSE_CFLAGS) -o mount.wfs

mkfs.wfs:
	$(CC) $(CFLAGS) -o mkfs.wfs mkfs.wfs.c

fsck.wfs:
	$(CC) $(CFLAGS) -o fsck.wfs fsck.wfs.c

.PHONY: clean
clean:
	rm -rf $(BINS)
