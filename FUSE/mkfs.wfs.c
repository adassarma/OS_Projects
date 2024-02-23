#include "wfs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Usage: %s disk_path\n", argv[0]);
        return 1;
    }

    char *disk_path = argv[1];

    int fd = open(disk_path, O_RDWR);
    if (fd < 0) {
        perror("");
        return errno;
    }

    struct stat st;
    stat(disk_path, &st);

    void *disk = mmap(0, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk < (void *) 0) {
        perror("");
        return errno;
    }

    struct wfs_sb* sb = disk;
    sb->magic = WFS_MAGIC;
    sb->head = sizeof(*sb);

    struct wfs_inode i = {
            .inode_number = 0,
            .deleted = 0,
            .mode = S_IFDIR | 0755,
            .uid = getuid(),
            .gid = getgid(),
            .atime = time(NULL),
            .mtime = time(NULL),
            .ctime = time(NULL),
            .links = 2,
            .size = 0,
    };

    struct wfs_log_entry root = {
        .inode = i,
    };

    memcpy((char *) disk + sb->head, &root, sizeof(root));
    sb->head += sizeof(root);

    char* c = getcwd(0, 0);
    size_t cl = strlen(c), dl = strlen(disk_path);

    c = realloc(c, cl + dl + 2); // +2 for '/' and '\0'
    char *fullpath = strcat(strcat(c, "/"), disk_path);
    printf("Created wfs filesystem!\n");
    printf("Full path: %s\n", fullpath);
    printf("Capacity: %luMiB\n", st.st_size >> 20);

    free(c);
    munmap(disk, st.st_size);
    close(fd);
}
