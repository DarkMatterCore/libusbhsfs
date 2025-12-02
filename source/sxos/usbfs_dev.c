/*
 * usbfs_dev.c
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2018, Team Xecuter.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <sys/iosupport.h>
#include <sys/param.h>
#include <fcntl.h>

#include "../usbhsfs_utils.h"
#include "usbfs_dev.h"

/* Helper macros. */

#define USBFS_INIT_FILE_VARS    DEVOPTAB_DECL_FILE_STATE(usbfsdev_file); \
                                DEVOPTAB_INIT_ERROR_STATE; \
                                if (!file) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL)

#define USBFS_INIT_DIR_VARS     DEVOPTAB_DECL_DIR_STATE(usbfsdev_dir); \
                                DEVOPTAB_INIT_ERROR_STATE; \
                                if (!dir) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL)

#define USBFS_INVALID_FILE_ID   UINT32_MAX

/* Type definitions. */

/// usbfs file state.
typedef struct {
    uint64_t fileid;
    int flags;
} usbfsdev_file;

/// usbfs directory state.
typedef struct {
    uint64_t dirid;
} usbfsdev_dir;

/* Function prototypes. */

static int       usbfsdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode);
static int       usbfsdev_close(struct _reent *r, void *fd);
static ssize_t   usbfsdev_write(struct _reent *r, void *fd, const char *ptr, size_t len);
static ssize_t   usbfsdev_read(struct _reent *r, void *fd, char *ptr, size_t len);
static off_t     usbfsdev_seek(struct _reent *r, void *fd, off_t pos, int dir);
static int       usbfsdev_fstat(struct _reent *r, void *fd, struct stat *st);
static int       usbfsdev_stat(struct _reent *r, const char *file, struct stat *st);
static int       usbfsdev_link(struct _reent *r, const char *existing, const char *newLink);
static int       usbfsdev_unlink(struct _reent *r, const char *name);
static int       usbfsdev_chdir(struct _reent *r, const char *name);
static int       usbfsdev_rename(struct _reent *r, const char *oldName, const char *newName);
static int       usbfsdev_mkdir(struct _reent *r, const char *path, int mode);
static DIR_ITER* usbfsdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path);
static int       usbfsdev_dirreset(struct _reent *r, DIR_ITER *dirState);
static int       usbfsdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat);
static int       usbfsdev_dirclose(struct _reent *r, DIR_ITER *dirState);
static int       usbfsdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf);
static int       usbfsdev_ftruncate(struct _reent *r, void *fd, off_t len);
static int       usbfsdev_fsync(struct _reent *r, void *fd);
static int       usbfsdev_chmod(struct _reent *r, const char *path, mode_t mode);
static int       usbfsdev_fchmod(struct _reent *r, void *fd, mode_t mode);
static int       usbfsdev_rmdir(struct _reent *r, const char *name);
static int       usbfsdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2]);
static long      usbfsdev_fpathconf(struct _reent *r, void *fd, int name);
static long      usbfsdev_pathconf(struct _reent *r, const char *path, int name);
static int       usbfsdev_symlink(struct _reent *r, const char *target, const char *linkpath);
static ssize_t   usbfsdev_readlink(struct _reent *r, const char *path, char *buf, size_t bufsiz);

/* Global variables. */

static const devoptab_t usbfsdev_devoptab = {
    .name         = USBFS_MOUNT_NAME,
    .structSize   = sizeof(usbfsdev_file),
    .open_r       = usbfsdev_open,
    .close_r      = usbfsdev_close,
    .write_r      = usbfsdev_write,
    .read_r       = usbfsdev_read,
    .seek_r       = usbfsdev_seek,
    .fstat_r      = usbfsdev_fstat,
    .stat_r       = usbfsdev_stat,
    .link_r       = usbfsdev_link,          ///< Not supported by usbfs.
    .unlink_r     = usbfsdev_unlink,
    .chdir_r      = usbfsdev_chdir,         ///< Not supported by usbfs.
    .rename_r     = usbfsdev_rename,        ///< Not supported by usbfs.
    .mkdir_r      = usbfsdev_mkdir,
    .dirStateSize = sizeof(usbfsdev_dir),
    .diropen_r    = usbfsdev_diropen,
    .dirreset_r   = usbfsdev_dirreset,      ///< Not supported by usbfs.
    .dirnext_r    = usbfsdev_dirnext,
    .dirclose_r   = usbfsdev_dirclose,
    .statvfs_r    = usbfsdev_statvfs,
    .ftruncate_r  = usbfsdev_ftruncate,
    .fsync_r      = usbfsdev_fsync,
    .deviceData   = NULL,
    .chmod_r      = usbfsdev_chmod,         ///< Not supported by usbfs.
    .fchmod_r     = usbfsdev_fchmod,        ///< Not supported by usbfs.
    .rmdir_r      = usbfsdev_rmdir,
    .lstat_r      = usbfsdev_stat,          ///< Symlinks aren't supported, so we'll just alias lstat() to stat().
    .utimes_r     = usbfsdev_utimes,        ///< Not supported by usbfs.
    .fpathconf_r  = usbfsdev_fpathconf,     ///< Not supported by usbfs.
    .pathconf_r   = usbfsdev_pathconf,      ///< Not supported by usbfs.
    .symlink_r    = usbfsdev_symlink,       ///< Not supported by usbfs.
    .readlink_r   = usbfsdev_readlink       ///< Not supported by usbfs.
};

bool usbfsdev_register(void)
{
    int device_idx = -1;
    bool ret = false;

    /* Get devoptab device index for our filesystem. */
    device_idx = FindDevice(USBFS_MOUNT_NAME ":");
    if (device_idx < 0)
    {
        /* Device not available. Let's add it. */
        device_idx = AddDevice(&usbfsdev_devoptab);
        if (device_idx < 0)
        {
            USBHSFS_LOG_MSG("Failed to add devoptab device for \"" USBFS_MOUNT_NAME "\"!");
            goto end;
        }
    }

    /* Update return value. */
    ret = true;

end:
    return ret;
}

void usbfsdev_unregister(void)
{
    RemoveDevice(USBFS_MOUNT_NAME ":");
}

static int usbfsdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(mode);

    Result rc = 0;

    USBFS_INIT_FILE_VARS;

    /* Sanity check. */
    if (!path || !*path || !(path = strchr(path, ':')) || !*(++path)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Reset file state. */
    file->fileid = USBFS_INVALID_FILE_ID;
    file->flags = flags;

    /* Open file. */
    rc = usbFsOpenFile(&(file->fileid), path, (u64)file->flags);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR(ENOENT);

end:
    DEVOPTAB_RETURN_INT(0);
}

static int usbfsdev_close(struct _reent *r, void *fd)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;

    USBFS_INIT_FILE_VARS;

    /* Close file. */
    rc = usbFsCloseFile(file->fileid);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Invalidate file ID. */
    file->fileid = USBFS_INVALID_FILE_ID;

end:
    DEVOPTAB_RETURN_INT(0);
}

static ssize_t usbfsdev_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;
    u64 pos = 0;

    USBFS_INIT_FILE_VARS;

    /* Sanity check. */
    if (!ptr || !len) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Check if the file was opened with write access. */
    if ((file->flags & O_ACCMODE) == O_RDONLY) DEVOPTAB_SET_ERROR_AND_EXIT(EBADF);

    /* Check if the append flag is enabled. */
    if (file->flags & O_APPEND)
    {
        /* Seek to EOF. */
        rc = usbFsSeekFile(file->fileid, 0, SEEK_END, &pos);
        if (R_FAILED(rc)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);
    }

    /* Write file data. */
    rc = usbFsWriteFile(file->fileid, ptr, len, (size_t*)&pos);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR(EINVAL);

end:
    DEVOPTAB_RETURN_INT((ssize_t)pos);
}

static ssize_t usbfsdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;
    size_t rd_sz = 0;

    USBFS_INIT_FILE_VARS;

    /* Sanity check. */
    if (!ptr || !len) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Check if the file was opened with read access. */
    if ((file->flags & O_ACCMODE) == O_WRONLY) DEVOPTAB_SET_ERROR_AND_EXIT(EBADF);

    /* Read file data. */
    rc = usbFsReadFile(file->fileid, ptr, len, &rd_sz);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR(EINVAL);

end:
    DEVOPTAB_RETURN_INT((ssize_t)rd_sz);
}

static off_t usbfsdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;
    u64 outpos = 0;

    USBFS_INIT_FILE_VARS;

    /* Perform file seek. */
    rc = usbFsSeekFile(file->fileid, (u64)pos, (u64)dir, &outpos);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR(EINVAL);

end:
    DEVOPTAB_RETURN_INT((off_t)outpos);
}

static int usbfsdev_fstat(struct _reent *r, void *fd, struct stat *st)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;
    u64 size = 0, mode = 0;

    USBFS_INIT_FILE_VARS;

    /* Sanity check. */
    if (!st) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Get file stats. */
    rc = usbFsStatFile(file->fileid, &size, &mode);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Fill stat info. */
    memset(st, 0, sizeof(struct stat));

    st->st_nlink = 1;
    st->st_mode = (mode_t)mode;
    st->st_size = (off_t)size;

end:
    DEVOPTAB_RETURN_INT(0);
}

static int usbfsdev_stat(struct _reent *r, const char *file, struct stat *st)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;
    u64 size = 0, mode = 0;

    DEVOPTAB_INIT_ERROR_STATE;

    /* Sanity check. */
    if (!file || !*file || !(file = strchr(file, ':')) || !*(++file) || !st) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Get file stats. */
    rc = usbFsStatPath(file, &size, &mode);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Fill stat info. */
    memset(st, 0, sizeof(struct stat));

    st->st_nlink = 1;
    st->st_mode = (mode_t)mode;
    st->st_size = (off_t)size;

end:
    DEVOPTAB_RETURN_INT(0);
}

static int usbfsdev_link(struct _reent *r, const char *existing, const char *newLink)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(existing);
    NX_IGNORE_ARG(newLink);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int usbfsdev_unlink(struct _reent *r, const char *name)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;

    DEVOPTAB_INIT_ERROR_STATE;

    /* Sanity check. */
    if (!name || !*name || !(name = strchr(name, ':')) || !*(++name)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Delete file. */
    rc = usbFsDeleteFile(name);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR(EINVAL);

end:
    DEVOPTAB_RETURN_INT(0);
}

static int usbfsdev_chdir(struct _reent *r, const char *name)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int usbfsdev_rename(struct _reent *r, const char *oldName, const char *newName)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(oldName);
    NX_IGNORE_ARG(newName);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int usbfsdev_mkdir(struct _reent *r, const char *path, int mode)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(mode);

    Result rc = 0;

    DEVOPTAB_INIT_ERROR_STATE;

    /* Sanity check. */
    if (!path || !*path || !(path = strchr(path, ':')) || !*(++path)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Create directory. */
    rc = usbFsCreateDir(path);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR(EINVAL);

end:
    DEVOPTAB_RETURN_INT(0);
}

static DIR_ITER *usbfsdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;
    DIR_ITER *ret = NULL;

    USBFS_INIT_DIR_VARS;

    /* Sanity check. */
    if (!path || !*path || !(path = strchr(path, ':')) || !*(++path)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Reset directory state. */
    memset(dir, 0, sizeof(usbfsdev_dir));

    /* Open directory. */
    rc = usbFsOpenDir(&(dir->dirid), path);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Update return value. */
    ret = dirState;

end:
    DEVOPTAB_RETURN_PTR(ret);
}

static int usbfsdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(dirState);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int usbfsdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;
    u64 type = 0, size = 0;

    USBFS_INIT_DIR_VARS;

    /* Sanity check. */
    if (!filename || !filestat) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Clear filename buffer. */
    memset(filename, 0, NAME_MAX);

    /* Read directory. */
    rc = usbFsReadDir(dir->dirid, &type, &size, filename, NAME_MAX);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR_AND_EXIT(rc == 0x68A ? ENOENT : EINVAL);  /* ENOENT signals EOD. */

    /* Clear stat struct. */
    memset(filestat, 0, sizeof(struct stat));

    /* Fill stat info. */
    filestat->st_nlink = 1;
    filestat->st_mode = (mode_t)type;
    filestat->st_size = (off_t)size;

end:
    DEVOPTAB_RETURN_INT(0);
}

static int usbfsdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;

    USBFS_INIT_DIR_VARS;

    /* Close directory. */
    rc = usbFsCloseDir(dir->dirid);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Reset directory state. */
    memset(dir, 0, sizeof(usbfsdev_dir));

end:
    DEVOPTAB_RETURN_INT(0);
}

static int usbfsdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(path);

    Result rc = 0;
    u64 freespace = 0, totalspace = 0;

    DEVOPTAB_INIT_ERROR_STATE;

    /* Sanity check. */
    if (!buf) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Get volume information. */
    rc = usbFsStatFilesystem(&totalspace, &freespace);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Fill filesystem stats. */
    memset(buf, 0, sizeof(struct statvfs));

    buf->f_bsize = 1;
    buf->f_frsize = 1;
    buf->f_blocks = totalspace;
    buf->f_bfree = freespace;
    buf->f_bavail = freespace;
    buf->f_files = 0;
    buf->f_ffree = 0;
    buf->f_favail = 0;
    buf->f_fsid = 0;
    buf->f_flag = ST_NOSUID;
    buf->f_namemax = 0;

end:
    DEVOPTAB_RETURN_INT(0);
}

static int usbfsdev_ftruncate(struct _reent *r, void *fd, off_t len)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;

    USBFS_INIT_FILE_VARS;

    /* Sanity check. */
    if (len < 0) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Check if the file was opened with write access. */
    if ((file->flags & O_ACCMODE) == O_RDONLY) DEVOPTAB_SET_ERROR_AND_EXIT(EBADF);

    /* Truncate file. */
    rc = usbFsTruncateFile(file->fileid, (u64)len);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR(EINVAL);

end:
    DEVOPTAB_RETURN_INT(0);
}

static int usbfsdev_fsync(struct _reent *r, void *fd)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;

    USBFS_INIT_FILE_VARS;

    /* Synchronize file. */
    rc = usbFsSyncFile(file->fileid);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR(EINVAL);

end:
    DEVOPTAB_RETURN_INT(0);
}

static int usbfsdev_chmod(struct _reent *r, const char *path, mode_t mode)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(mode);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int usbfsdev_fchmod(struct _reent *r, void *fd, mode_t mode)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(fd);
    NX_IGNORE_ARG(mode);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int usbfsdev_rmdir(struct _reent *r, const char *name)
{
    NX_IGNORE_ARG(r);

    Result rc = 0;

    DEVOPTAB_INIT_ERROR_STATE;

    /* Sanity check. */
    if (!name || !*name || !(name = strchr(name, ':')) || !*(++name)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Delete directory. */
    rc = usbFsDeleteDir(name);
    if (R_FAILED(rc)) DEVOPTAB_SET_ERROR(EINVAL);

end:
    DEVOPTAB_RETURN_INT(0);
}

static int usbfsdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2])
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(filename);
    NX_IGNORE_ARG(times);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static long usbfsdev_fpathconf(struct _reent *r, void *fd, int name)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(fd);
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static long usbfsdev_pathconf(struct _reent *r, const char *path, int name)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int usbfsdev_symlink(struct _reent *r, const char *target, const char *linkpath)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(target);
    NX_IGNORE_ARG(linkpath);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static ssize_t usbfsdev_readlink(struct _reent *r, const char *path, char *buf, size_t bufsiz)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(buf);
    NX_IGNORE_ARG(bufsiz);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}
