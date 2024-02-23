#include <stddef.h>
#include <stdint.h>

#ifndef MOUNT_WFS_H_
#define MOUNT_WFS_H_

#define MAX_FILE_NAME_LEN 32
#define WFS_MAGIC 0xdeadbeef

struct wfs_sb {
    uint32_t magic;
    uint32_t head;
};

struct wfs_private {
    int fd;
    void *disk;
    unsigned long len;
    size_t head;
};

struct wfs_inode {
    unsigned int inode_number;
    unsigned int deleted;
    unsigned int mode;
    unsigned int uid;
    unsigned int gid;
    unsigned int flags;
    unsigned int size;
    unsigned int atime;
    unsigned int mtime;
    unsigned int ctime;
    unsigned int links;
};

struct wfs_dentry {
    char name[MAX_FILE_NAME_LEN];
    unsigned long inode_number;
};

struct wfs_log_entry {
    struct wfs_inode inode;
    char data[];
};

struct wfs_inode *get_inode(unsigned int ino);
unsigned long inode_for_name(char *name, struct wfs_log_entry *l);
struct wfs_inode *path_to_inode(const char *path);

#endif
