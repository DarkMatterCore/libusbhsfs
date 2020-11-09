/*
 * usbhsfs_drive.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * libusbhsfs is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * libusbhsfs is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ff.h"
#include "fat_dev.h"
#include <errno.h>
#include <sys/iosupport.h>
#include <sys/param.h>
#include <fcntl.h>

static int usbHsFsFatConvertErrorCode(FRESULT res)
{
    switch(res)
    {
        case FR_OK:
            return 0;
        /* TODO: more */
        default:
            return EINVAL;
    }
}

typedef struct {
    FIL ff_file;
} FatFileObject;

typedef struct {
    DIR ff_dir;
} FatDirObject;

static int fatdev_open(struct _reent *r, void *fileStruct, const char *path, int flags, int mode)
{
    FatFileObject *fat_file = (FatFileObject*)fileStruct;
    
    BYTE ff_mode = 0;
    if (flags & O_RDONLY)
    {
        ff_mode = FA_READ | FA_OPEN_EXISTING;
    }
    else if (flags & O_WRONLY)
    {
        ff_mode = FA_WRITE | FA_OPEN_APPEND;
    }
    if (flags & O_CREAT) ff_mode |= FA_CREATE_NEW;
    
    FRESULT ff_res = f_open(&fat_file->ff_file, path, ff_mode);
    if (ff_res == FR_OK) return 0;
    else
    {
        r->_errno = usbHsFsFatConvertErrorCode(ff_res);
        return -1;
    }
}

static int fatdev_close(struct _reent *r, void *fd)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static ssize_t fatdev_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

/*
static ssize_t fatdev_write_safe(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    r->_errno = EINVAL;
    return -1;
}
*/

static ssize_t fatdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

/*
static ssize_t fatdev_read_safe(struct _reent *r, void *fd, char *ptr, size_t len)
{
    r->_errno = EINVAL;
    return -1;
}
*/

static off_t fatdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_fstat(struct _reent *r, void *fd, struct stat *st)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_stat(struct _reent *r, const char *file, struct stat *st)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_link(struct _reent *r, const char *existing, const char  *newLink)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_unlink(struct _reent *r, const char *name)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_chdir(struct _reent *r, const char *name)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_rename(struct _reent *r, const char *oldName, const char *newName)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_mkdir(struct _reent *r, const char *path, int mode)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static DIR_ITER *fatdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    /* TODO */
    r->_errno = EINVAL;
    return NULL;
}

static int fatdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_ftruncate(struct _reent *r, void *fd, off_t len)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_fsync(struct _reent *r, void *fd)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_chmod(struct _reent *r, const char *path, mode_t mode)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_fchmod(struct _reent *r, void *fd, mode_t mode)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_rmdir(struct _reent *r, const char *name)
{
    /* TODO */
    r->_errno = EINVAL;
    return -1;
}

static const devoptab_t g_FatDevoptab =
{
  .structSize   = sizeof(FatFileObject),
  .open_r       = fatdev_open,
  .close_r      = fatdev_close,
  .write_r      = fatdev_write,
  .read_r       = fatdev_read,
  .seek_r       = fatdev_seek,
  .fstat_r      = fatdev_fstat,
  .stat_r       = fatdev_stat,
  .link_r       = fatdev_link,
  .unlink_r     = fatdev_unlink,
  .chdir_r      = fatdev_chdir,
  .rename_r     = fatdev_rename,
  .mkdir_r      = fatdev_mkdir,
  .dirStateSize = sizeof(FatDirObject),
  .diropen_r    = fatdev_diropen,
  .dirreset_r   = fatdev_dirreset,
  .dirnext_r    = fatdev_dirnext,
  .dirclose_r   = fatdev_dirclose,
  .statvfs_r    = fatdev_statvfs,
  .ftruncate_r  = fatdev_ftruncate,
  .fsync_r      = fatdev_fsync,
  .deviceData   = 0,
  .chmod_r      = fatdev_chmod,
  .fchmod_r     = fatdev_fchmod,
  .rmdir_r      = fatdev_rmdir,
  // symlinks aren't supported so alias lstat to stat
  .lstat_r      = fatdev_stat,
};

const devoptab_t usbHsFsFatGetDevoptab()
{
    return g_FatDevoptab;
}