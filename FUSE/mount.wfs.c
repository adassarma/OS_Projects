#include "wfs.h"

#define FUSE_USE_VERSION 30

#include <assert.h>
#include <errno.h>
#include <fuse.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct wfs_private wp;
unsigned int next_inode = 1;

int wfs_unlink(const char *path)
{
    struct wfs_inode *i = path_to_inode(path);
    if (!i)
        return -1;

    // Simplification, not creating a new log entry, but changing the old one.
    i->deleted = 1;

    char pp[128];
    strcpy(pp, path);
    struct wfs_inode *pi = path_to_inode(dirname(pp));
    struct wfs_log_entry *l = (void *) pi;
    strcpy(pp, path);

    size_t new_size = sizeof(struct wfs_log_entry) + pi->size - sizeof(struct wfs_dentry);
    struct wfs_log_entry *e = malloc(new_size);
    memcpy(e, pi, sizeof(*pi));

    // for over dentries
    size_t dentries = pi->size / sizeof(struct wfs_dentry);
    struct wfs_dentry *old = (void *) l->data;
    struct wfs_dentry *new = (void *) e->data;
    for (size_t i = 0; i < dentries; i++) {
        if (!strcmp(old->name, basename(pp))) {
            old++;
            continue;
        }

        *new = *old;
        new++;
        old++;
    }

    e->inode.size -= sizeof(struct wfs_dentry);

    memcpy((char *)wp.disk + wp.head, e, new_size);
    wp.head += new_size;
    free(e);

    return 0;
}

// FIXME: Just overwrites work.
int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct wfs_inode *orig = path_to_inode(path);
    struct wfs_log_entry *e = (void *) orig;

    size_t data_size = orig->size > size + offset ? orig->size : size + offset;

    char *data = malloc(data_size);

    memcpy(data, e->data, orig->size);

    memcpy(data + offset, buf, size);

    struct wfs_log_entry new_e = *e;
    new_e.inode.size = data_size;

    memcpy((char *)wp.disk + wp.head, &new_e, sizeof(new_e));
    wp.head += sizeof(new_e);
    memcpy((char *)wp.disk + wp.head, data, data_size);
    wp.head += data_size;

    free(data);

    return size;
}

static int wfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    if (S_ISREG(mode)){
        char pp[128];
        strcpy(pp, path);
        struct wfs_inode *pi = path_to_inode(dirname(pp));
        struct wfs_log_entry *l = (void *) pi;
        strcpy(pp, path);

        size_t new_size = sizeof(struct wfs_log_entry) + pi->size + sizeof(struct wfs_dentry);
        struct wfs_log_entry *e = malloc(new_size);
        memcpy(e, pi, sizeof(*pi));
        memcpy(e->data, l->data, pi->size);
        e->inode.size += sizeof(struct wfs_dentry);

        struct wfs_dentry *d = (void *)(e->data + pi->size);
        strcpy(d->name, basename(pp));
        d->inode_number = next_inode++;

        if (wp.head + new_size > wp.len) {
            next_inode++;
            free(e);
            return -ENOSPC;
        }

        memcpy((char *)wp.disk + wp.head, e, new_size);
        wp.head += new_size;
        free(e);

        struct wfs_inode i = {
            .deleted = 0,
            .inode_number = next_inode-1,
            .mode = S_IFREG | mode,
            .uid = getuid(),
            .gid = getgid(),
            .atime = time(NULL),
            .mtime = time(NULL),
            .ctime = time(NULL),
            .links = 1,
            .size = 0,
        };

        if (wp.head + sizeof(i) > wp.len) {
            next_inode--;
            wp.head -= new_size;
            return -ENOSPC;
        }

        memcpy((char *)wp.disk + wp.head, &i, sizeof(i));
        wp.head += sizeof(i);

    }

    return 0;
}

struct wfs_inode *path_to_inode(const char *path)
{
    struct wfs_inode *root = get_inode(0);
    struct wfs_log_entry *l = (void *) root;

    char p[128];
    strcpy(p, path);
    char *token = strtok(p, "/");

    unsigned long ino = 0;
    while (token != NULL) {
        ino = inode_for_name(token, l);
        if (ino == 0) {
            return NULL;
        }

        root = get_inode(ino);
        l = (void *) root;
        token = strtok(NULL, "/");
    }

    return get_inode(ino);
}

static int wfs_mkdir(const char *path, mode_t mode)
{
    char pp[128];
    strcpy(pp, path);
    struct wfs_inode *pi = path_to_inode(dirname(pp));
    struct wfs_log_entry *l = (void *) pi;
    strcpy(pp, path);

    size_t new_size = sizeof(struct wfs_log_entry) + pi->size + sizeof(struct wfs_dentry);
    struct wfs_log_entry *e = malloc(new_size);
    memcpy(e, pi, sizeof(*pi));
    memcpy(e->data, l->data, pi->size);
    e->inode.size += sizeof(struct wfs_dentry);

    struct wfs_dentry *d = (void *)(e->data + pi->size);
    strcpy(d->name, basename(pp));
    d->inode_number = next_inode++;

    if (wp.head + new_size > wp.len) {
        next_inode++;
        free(e);
        return -ENOSPC;
    }

    memcpy((char *)wp.disk + wp.head, e, new_size);
    wp.head += new_size;
    free(e);

    struct wfs_inode i = {
        .deleted = 0,
        .inode_number = next_inode-1,
        .mode = S_IFDIR | mode,
        .uid = getuid(),
        .gid = getgid(),
        .atime = time(NULL),
        .mtime = time(NULL),
        .ctime = time(NULL),
        .links = 1,
        .size = 0,
    };

    if (wp.head + sizeof(i) > wp.len) {
        next_inode--;
        wp.head -= new_size;
        return -ENOSPC;
    }

    memcpy((char *)wp.disk + wp.head, &i, sizeof(i));
    wp.head += sizeof(i);

    return 0;
}

struct wfs_inode *get_inode(unsigned int ino)
{
    uintptr_t start = sizeof(struct wfs_sb);
    uintptr_t stop = wp.head;
    struct wfs_log_entry *latest = NULL;

    while (start < stop) {
        struct wfs_log_entry *e = (void *)((char *) wp.disk + start);
        if (e->inode.inode_number == ino)
            latest = e;

        start += sizeof(struct wfs_inode) + e->inode.size;
    }

    if (latest && latest->inode.deleted == 0)
        return &latest->inode;

    return NULL;
}

char *get_data(struct wfs_log_entry *l)
{
    if (l->inode.size == 0)
        return NULL;

    if ((l->inode.mode & S_IFMT) != S_IFREG)
        return NULL;

    return l->data;
}

struct wfs_dentry *get_dentries(struct wfs_log_entry *l)
{
    if (l->inode.size == 0)
        return NULL;

    if ((l->inode.mode & S_IFMT) != S_IFDIR)
        return NULL;

    struct wfs_dentry *d = (void *) l->data;
    return d;
}

unsigned long inode_for_name(char *name, struct wfs_log_entry *l)
{
    struct wfs_dentry *d = get_dentries(l);
    size_t entries = l->inode.size / sizeof(struct wfs_dentry);

    for (size_t i = 0; i < entries; i++) {
        if (!strcmp((d+i)->name, name))
            return (d+i)->inode_number;
    }

    return 0;
}

static int wfs_rmdir(const char *path)
{
  struct wfs_inode *i = path_to_inode(path);
  if (!i)
    return -ENOENT;

  if (i->size / sizeof(struct wfs_dentry) > 0)
    return -ENOTEMPTY;

  return wfs_unlink(path);
}


static int wfs_getattr( const char *path, struct stat *st )
{
        struct wfs_inode *i = path_to_inode(path);
        if (!i)
            return -ENOENT;

        memset(st, 0, sizeof(*st));
        st->st_uid = i->uid;
        st->st_gid = i->gid;
        st->st_atime = i->atime;
        st->st_mtime = i->mtime;
        st->st_mode = i->mode;
        st->st_nlink = i->links;
        st->st_size = i->size;

        return 0;
}

static int wfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);

        struct wfs_inode *i = path_to_inode(path);
        struct wfs_log_entry *l = (void *)i;
        struct wfs_dentry *d = (void *)l->data;

        size_t entries = i->size / sizeof(struct wfs_dentry);

        assert(i->size % sizeof(struct wfs_dentry) == 0);

        for (size_t i = 0; i < entries; i++)
            filler(buffer, (d+i)->name, NULL, 0 );
	
	return 0;
}

static int wfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
        struct wfs_inode *i = path_to_inode(path);
        if (!i)
            return -ENOENT;

        struct wfs_log_entry *l = (void *) i;
        if (offset > i->size)
            offset = i->size;
        if (size + offset > i->size)
            size = i->size - offset;

        memcpy(buffer, get_data(l) + offset, size);

	return size;
}

static struct fuse_operations ops = {
    .getattr	= wfs_getattr,
    .mknod      = wfs_mknod,
    .mkdir      = wfs_mkdir,
    .read	= wfs_read,
    .write      = wfs_write,
    .readdir	= wfs_readdir,
    .unlink    	= wfs_unlink,
    .rmdir      = wfs_rmdir,
};

void wfs_usage(char *prog)
{
    printf("Usage: %s [FUSE options] disk_path mount_point\n", prog);
}

int main(int argc, char *argv[])
{
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-')) {
        wfs_usage(argv[0]);
        return 1;
    }

    char *disk_path = argv[argc-2];
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;

    wp.fd = open(disk_path, O_RDWR);
    if (wp.fd < 0) {
        perror("");
        return errno;
    }

    struct stat st;
    stat(disk_path, &st);

    wp.disk = mmap(0, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, wp.fd, 0);
    if (wp.disk < (void *)0) {
        perror("");
        return errno;
    }

    struct wfs_sb* sb = wp.disk;
    if (sb->magic != WFS_MAGIC) {
        printf("Not a wfs filesystem, refusing to mount!\n");
        goto FINISH;
    }

    wp.len = st.st_size;
    wp.head = sb->head;

    fuse_main(argc, argv, &ops, NULL);
    sb->head = wp.head;

FINISH:
    munmap(wp.disk, wp.len);
    close(wp.fd);

    return 0;
}
