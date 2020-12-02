/*
 * ntfs_dev.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 * This file is based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#include "ntfs.h"
#include "ntfs_dev.h"

static int       ntfsdev_open(struct _reent *r, void *fileStruct, const char *path, int flags, int mode);
static int       ntfsdev_close(struct _reent *r, void *fd);
static ssize_t   ntfsdev_write(struct _reent *r, void *fd, const char *ptr, size_t len);
static ssize_t   ntfsdev_read(struct _reent *r, void *fd, char *ptr, size_t len);
static off_t     ntfsdev_seek(struct _reent *r, void *fd, off_t pos, int dir);
static int       ntfsdev_fstat(struct _reent *r, void *fd, struct stat *st);
static int       ntfsdev_stat(struct _reent *r, const char *file, struct stat *st);
static int       ntfsdev_link(struct _reent *r, const char *existing, const char *newLink);
static int       ntfsdev_unlink(struct _reent *r, const char *name);
static int       ntfsdev_chdir(struct _reent *r, const char *name);
static int       ntfsdev_rename(struct _reent *r, const char *oldName, const char *newName);
static int       ntfsdev_mkdir(struct _reent *r, const char *path, int mode);
static DIR_ITER* ntfsdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path);
static int       ntfsdev_dirreset(struct _reent *r, DIR_ITER *dirState);
static int       ntfsdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat);
static int       ntfsdev_dirclose(struct _reent *r, DIR_ITER *dirState);
static int       ntfsdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf);
static int       ntfsdev_ftruncate(struct _reent *r, void *fd, off_t len);
static int       ntfsdev_fsync(struct _reent *r, void *fd);
static int       ntfsdev_chmod(struct _reent *r, const char *path, mode_t mode);
static int       ntfsdev_fchmod(struct _reent *r, void *fd, mode_t mode);
static int       ntfsdev_rmdir(struct _reent *r, const char *name);
static int       ntfsdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2]);

static const devoptab_t ntfsdev_devoptab = {
    .name         = NULL,
    .structSize   = sizeof(ntfs_file_state),
    .open_r       = ntfsdev_open,
    .close_r      = ntfsdev_close,
    .write_r      = ntfsdev_write,
    .read_r       = ntfsdev_read,
    .seek_r       = ntfsdev_seek,
    .fstat_r      = ntfsdev_fstat,
    .stat_r       = ntfsdev_stat,
    .link_r       = ntfsdev_link,
    .unlink_r     = ntfsdev_unlink,
    .chdir_r      = ntfsdev_chdir,
    .rename_r     = ntfsdev_rename,
    .mkdir_r      = ntfsdev_mkdir,
    .dirStateSize = sizeof(ntfs_dir_state),
    .diropen_r    = ntfsdev_diropen,
    .dirreset_r   = ntfsdev_dirreset,
    .dirnext_r    = ntfsdev_dirnext,
    .dirclose_r   = ntfsdev_dirclose,
    .statvfs_r    = ntfsdev_statvfs,
    .ftruncate_r  = ntfsdev_ftruncate,
    .fsync_r      = ntfsdev_fsync,
    .deviceData   = NULL,
    .chmod_r      = ntfsdev_chmod,
    .fchmod_r     = ntfsdev_fchmod,
    .rmdir_r      = ntfsdev_rmdir,
    .lstat_r      = ntfsdev_stat,
    .utimes_r     = ntfsdev_utimes
};

const devoptab_t *ntfsdev_get_devoptab()
{
    return &ntfsdev_devoptab;
}


int ntfsdev_open (struct _reent *r, void *fileStruct, const char *path, int flags, int mode)
{
    ntfs_log_trace("fileStruct %p, path %s, flags %i, mode %i\n", (void *) fileStruct, path, flags, mode);

    // TODO: This...

    return 0;
}

int ntfsdev_close (struct _reent *r, void *fd)
{
    ntfs_log_trace("fd %p\n", fd);

    // TODO: This...

    return 0;
}

ssize_t ntfsdev_write (struct _reent *r, void *fd, const char *ptr, size_t len)
{
    ntfs_log_trace("fd %p, ptr %p, len %lu\n", fd, ptr, len);

    // TODO: This...

    return 0;
}

ssize_t ntfsdev_read (struct _reent *r, void *fd, char *ptr, size_t len)
{
    ntfs_log_trace("fd %p, ptr %p, len %lu\n", fd, ptr, len);

    // TODO: This...

    return 0;
}

off_t ntfsdev_seek (struct _reent *r, void *fd, off_t pos, int dir)
{
    ntfs_log_trace("fd %p, pos %li, dir %i\n", fd, pos, dir);

    // TODO: This...

    return 0;
}
int ntfsdev_fstat (struct _reent *r, void *fd, struct stat *st)
{
    ntfs_log_trace("fd %p\n", fd);

    // TODO: This...

    return 0;
}

int ntfsdev_stat (struct _reent *r, const char *path, struct stat *st)
{
    ntfs_log_trace("path %s, st %p\n", path, st);

    // TODO: This...

    return 0;
}

int ntfsdev_link (struct _reent *r, const char *existing, const char *newLink)
{
    ntfs_log_trace("existing %s, newLink %s\n", existing, newLink);

    // TODO: This...

    return 0;
}

int ntfsdev_unlink (struct _reent *r, const char *name)
{
    ntfs_log_trace("name %s\n", name);

    // TODO: This...

    return 0;
}

int ntfsdev_chdir (struct _reent *r, const char *name)
{
    ntfs_log_trace("name %s\n", name);

    // TODO: This...

    return 0;
}

int ntfsdev_rename (struct _reent *r, const char *oldName, const char *newName)
{
    ntfs_log_trace("oldName %s, newName %s\n", oldName, newName);

    // TODO: This...

    return 0;
}

int ntfsdev_mkdir (struct _reent *r, const char *path, int mode)
{
    ntfs_log_trace("path %s, mode %i\n", path, mode);

    // TODO: This...

    return 0;
}

DIR_ITER *ntfsdev_diropen (struct _reent *r, DIR_ITER *dirState, const char *path)
{
    ntfs_log_trace("dirState %p, path %s\n", dirState, path);

    // TODO: This...

    return dirState;
}

int ntfsdev_dirreset (struct _reent *r, DIR_ITER *dirState)
{
    ntfs_log_trace("dirState %p\n", dirState);

    // TODO: This...

    return 0;
}

int ntfsdev_dirnext (struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    ntfs_log_trace("dirState %p, filename %p, filestat %p\n", dirState, filename, filestat);

    // TODO: This...

    return 0;
}

int ntfsdev_dirclose (struct _reent *r, DIR_ITER *dirState)
{
    ntfs_log_trace("dirState %p\n", dirState);

    // TODO: This...

    return 0;
}

int ntfsdev_statvfs (struct _reent *r, const char *path, struct statvfs *buf)
{
    ntfs_log_trace("path %s, buf %p\n", path, buf);

    // TODO: This...

    return 0;
}

int ntfsdev_ftruncate (struct _reent *r, void *fd, off_t len)
{
    ntfs_log_trace("fd %p, len %lu\n", fd, (u64) len);

    // TODO: This...

    return 0;
}

int ntfsdev_fsync (struct _reent *r, void *fd)
{
    ntfs_log_trace("fd %p\n", fd);

    // TODO: This...

    return 0;
}

int ntfsdev_chmod (struct _reent *r, const char *path, mode_t mode)
{
    ntfs_log_trace("path %s, mode %i\n", path, mode);

    // TODO: This...

    return 0;
}

int ntfsdev_fchmod (struct _reent *r, void *fd, mode_t mode)
{
    ntfs_log_trace("fd %p, mode %i\n", fd, mode);

    // TODO: This...

    return 0;
}

int ntfsdev_rmdir (struct _reent *r, const char *name)
{
    ntfs_log_trace("name %s\n", name);

    // TODO: This...

    return 0;
}

int ntfsdev_utimes (struct _reent *r, const char *filename, const struct timeval times[2])
{
    ntfs_log_trace("filename %s, time[0] %li, time[1] %li\n", filename, times[0].tv_sec, times[1].tv_sec);

    // TODO: This...

    return 0;
}
