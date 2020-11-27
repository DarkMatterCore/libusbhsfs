/*
 * usbfs_dev.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
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

/* Type definitions. */

typedef struct {
    uint64_t fileid;
    int flags;
} usbfsdev_file;

typedef struct
{
    uint64_t dirid;
} usbfsdev_dir;

/* Function prototypes. */

static int       usbfsdev_open(struct _reent *r, void *fileStruct, const char *path, int flags, int mode);
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
    .utimes_r     = usbfsdev_utimes         ///< Not supported by usbfs.
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
            USBHSFS_LOG("Failed to add devoptab device for \"" USBFS_MOUNT_NAME "\"!");
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

static int usbfsdev_open(struct _reent *r, void *fileStruct, const char *path, int flags, int mode)
{
    Result rc = 0;
    usbfsdev_file *file = (usbfsdev_file*)fileStruct;
    int ret = -1;
    
    char *pathAtColon = strchr(path, ':');
    if (pathAtColon) path = (pathAtColon + 1);

    memset(file, 0, sizeof(usbfsdev_file));
    file->flags = flags;
    
    rc = usbFsOpenFile(&(file->fileid), path, (u64)flags);
    if (R_SUCCEEDED(rc))
    {
        ret = 0;
    } else {
        r->_errno = ENOENT;
    }
    
    return ret;
}

static int usbfsdev_close(struct _reent *r, void *fd)
{
    Result rc = 0;
    usbfsdev_file *file = (usbfsdev_file*)fd;
    int ret = -1;
    
    rc = usbFsCloseFile(file->fileid);
    if (R_SUCCEEDED(rc))
    {
        file->fileid = 0xFFFFFFFF;
        ret = 0;
    } else {
        r->_errno = EINVAL;
    }
    
    return ret;
}

static ssize_t usbfsdev_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    Result rc = 0;
    usbfsdev_file *file = (usbfsdev_file*)fd;
    ssize_t ret = -1;
    u64 pos = 0;
    
    if ((file->flags & O_ACCMODE) == O_RDONLY)
    {
        r->_errno = EBADF;
        goto end;
    }
    
    if (file->flags & O_APPEND)
    {
        rc = usbFsSeekFile(file->fileid, 0, SEEK_END, &pos);
        if (R_FAILED(rc))
        {
            r->_errno = EINVAL;
            goto end;
        }
    }
    
    rc = usbFsWriteFile(file->fileid, ptr, len, (size_t*)&pos);
    if (R_SUCCEEDED(rc))
    {
        ret = (ssize_t)pos;
    } else {
        r->_errno = EINVAL;
    }
    
end:
    return ret;
}

static ssize_t usbfsdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    Result rc = 0;
    usbfsdev_file *file = (usbfsdev_file*)fd;
    ssize_t ret = -1;
    size_t rd_sz = 0;
    
    if ((file->flags & O_ACCMODE) == O_WRONLY)
    {
        r->_errno = EBADF;
        goto end;
    }
    
    rc = usbFsReadFile(file->fileid, ptr, len, &rd_sz);
    if (R_SUCCEEDED(rc))
    {
        ret = (ssize_t)rd_sz;
    } else {
        r->_errno = EINVAL;
    }
    
end:
    return ret;
}

static off_t usbfsdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    Result rc = 0;
    usbfsdev_file *file = (usbfsdev_file*)fd;
    off_t ret = -1;
    u64 outpos = 0;
    
    rc = usbFsSeekFile(file->fileid, (u64)pos, (u64)dir, &outpos);
    if (R_SUCCEEDED(rc))
    {
        ret = (off_t)outpos;
    } else {
        r->_errno = EINVAL;
    }
    
    return ret;
}

static int usbfsdev_fstat(struct _reent *r, void *fd, struct stat *st)
{
    Result rc = 0;
    usbfsdev_file* file = (usbfsdev_file*)fd;
    u64 size = 0, mode = 0;
    int ret = -1;
    
    rc = usbFsStatFile(file->fileid, &size, &mode);
    if (R_SUCCEEDED(rc))
    {
        memset(st, 0, sizeof(struct stat));
        st->st_nlink = 1;
        st->st_size = (off_t)size;
        st->st_mode = (mode_t)mode;
        ret = 0;
    } else {
        r->_errno = EINVAL;
    }
    
    return ret;
}

static int usbfsdev_stat(struct _reent *r, const char *file, struct stat *st)
{
    Result rc = 0;
    u64 size = 0, mode = 0;
    int ret = -1;
    
    char *pathAtColon = strchr(file, ':');
    if (pathAtColon) file = (pathAtColon + 1);
    
    rc = usbFsStatPath(file, &size, &mode);
    if (R_SUCCEEDED(rc))
    {
        memset(st, 0, sizeof(struct stat));
        st->st_nlink = 1;
        st->st_size = (off_t)size;
        st->st_mode = (mode_t)mode;
        ret = 0;
    } else {
        r->_errno = EINVAL;
    }
    
    return ret;
}

static int usbfsdev_link(struct _reent *r, const char *existing, const char *newLink)
{
    /* Not supported by usbfs. */
    r->_errno = ENOSYS;
    return -1;
}

static int usbfsdev_unlink(struct _reent *r, const char *name)
{
    Result rc = 0;
    int ret = -1;
    
    char *pathAtColon = strchr(name, ':');
    if (pathAtColon) name = (pathAtColon + 1);
    
    rc = usbFsDeleteFile(name);
    if (R_SUCCEEDED(rc))
    {
        ret = 0;
    } else {
        r->_errno = EINVAL;
    }

    return ret;
}

static int usbfsdev_chdir(struct _reent *r, const char *name)
{
    /* Not supported by usbfs. */
    r->_errno = ENOSYS;
    return -1;
}

static int usbfsdev_rename(struct _reent *r, const char *oldName, const char *newName)
{
    /* Not supported by usbfs. */
    r->_errno = ENOSYS;
    return -1;
}

static int usbfsdev_mkdir(struct _reent *r, const char *path, int mode)
{
    Result rc = 0;
    int ret = -1;
    
    char *pathAtColon = strchr(path, ':');
    if (pathAtColon) path = (pathAtColon + 1);
    
    rc = usbFsCreateDir(path);
    if (R_SUCCEEDED(rc))
    {
        ret = 0;
    } else {
        r->_errno = EINVAL;
    }
    
    return ret;
}

static DIR_ITER *usbfsdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    Result rc = 0;
    usbfsdev_dir *d = (usbfsdev_dir*)dirState->dirStruct;
    DIR_ITER *ret = NULL;
    
    char *pathAtColon = strchr(path, ':');
    if (pathAtColon) path = (pathAtColon + 1);
    
    rc = usbFsOpenDir(&(d->dirid), path);
    if (R_SUCCEEDED(rc))
    {
        ret = dirState;
    } else {
        r->_errno = EINVAL;
    }
    
    return ret;
}

static int usbfsdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    /* Not supported by usbfs. */
    r->_errno = ENOSYS;
    return -1;
}

static int usbfsdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    Result rc = 0;
    usbfsdev_dir *d = (usbfsdev_dir*)(dirState->dirStruct);
    u64 type = 0, size = 0;
    int ret = -1;
    
    memset(filename, 0, NAME_MAX);
    
    rc = usbFsReadDir(d->dirid, &type, &size, filename, NAME_MAX);
    if (R_SUCCEEDED(rc))
    {
        filestat->st_ino = 0;
        filestat->st_mode = (mode_t)type;
        filestat->st_size = (off_t)size;
        ret = 0;
    } else {
        /* ENOENT signals EOD. */
        r->_errno = (rc == 0x68A ? ENOENT : EINVAL);
    }
    
    return ret;
}

static int usbfsdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    Result rc = 0;
    usbfsdev_dir *d = (usbfsdev_dir*)dirState->dirStruct;
    int ret = -1;
    
    rc = usbFsCloseDir(d->dirid);
    if (R_SUCCEEDED(rc))
    {
        ret = 0;
    } else {
        r->_errno = EINVAL;
    }
    
    return ret;
}

static int usbfsdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{
    Result rc = 0;
    u64 freespace = 0, totalspace = 0;
    int ret = -1;
    
    rc = usbFsStatFilesystem(&totalspace, &freespace);
    if (R_SUCCEEDED(rc))
    {
        memset(buf, 0, sizeof(struct statvfs));
        buf->f_bsize   = 1;
        buf->f_frsize  = 1;
        buf->f_blocks  = totalspace;
        buf->f_bfree   = freespace;
        buf->f_bavail  = freespace;
        buf->f_files   = 0;
        buf->f_ffree   = 0;
        buf->f_favail  = 0;
        buf->f_fsid    = 0;
        buf->f_flag    = ST_NOSUID;
        buf->f_namemax = 0;
        ret = 0;
    } else {
        r->_errno = EINVAL;
    }
    
    return ret;
}

static int usbfsdev_ftruncate(struct _reent *r, void *fd, off_t len)
{
    Result rc = 0;
    usbfsdev_file *file = (usbfsdev_file*)fd;
    int ret = -1;
    
    if (len < 0)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    rc = usbFsTruncateFile(file->fileid, len);
    if (R_SUCCEEDED(rc))
    {
        ret = 0;
    } else {
        r->_errno = EINVAL;
    }
    
end:
    return ret;
}

static int usbfsdev_fsync(struct _reent *r, void *fd)
{
    Result rc = 0;
    usbfsdev_file *file = (usbfsdev_file*)fd;
    int ret = -1;
    
    rc = usbFsSyncFile(file->fileid);
    if (R_SUCCEEDED(rc))
    {
        ret = 0;
    } else {
        r->_errno = EINVAL;
    }
    
    return ret;
}

static int usbfsdev_chmod(struct _reent *r, const char *path, mode_t mode)
{
    /* Not supported by usbfs. */
    r->_errno = ENOSYS;
    return -1;
}

static int usbfsdev_fchmod(struct _reent *r, void *fd, mode_t mode)
{
    /* Not supported by usbfs. */
    r->_errno = ENOSYS;
    return -1;
}

static int usbfsdev_rmdir(struct _reent *r, const char *name)
{
    Result rc = 0;
    int ret = -1;
    
    char *pathAtColon = strchr(name, ':');
    if (pathAtColon) name = (pathAtColon + 1);
    
    rc = usbFsDeleteDir(name);
    if (R_SUCCEEDED(rc))
    {
        ret = 0;
    } else {
        r->_errno = EINVAL;
    }
    
    return ret;
}

static int usbfsdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2])
{
    /* Not supported by usbfs. */
    r->_errno = ENOSYS;
    return -1;
}
