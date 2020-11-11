/*
 * fat_dev.c
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
    USBHSFS_LOG("FRESULT: %d\n", res);
    switch(res)
    {
        case FR_OK:
            return 0;
        case FR_EXIST:
            return EEXIST;
        case FR_DISK_ERR:
        case FR_NO_FILESYSTEM:
        case FR_INVALID_DRIVE:
            return ENODEV;
        case FR_NO_FILE:
        case FR_NO_PATH:
            return ENOENT;
        case FR_INVALID_NAME:
        case FR_INVALID_PARAMETER:
            return EINVAL;
        /* TODO: more */
        default:
            return EINVAL;
    }
}

static void usbHsFsFatFillStat(struct stat *out_st, FILINFO *fil_info)
{
    out_st->st_size = fil_info->fsize;
    if(fil_info->fattrib & AM_DIR) out_st->st_mode |= S_IFDIR;
    else out_st->st_mode |= S_IFREG;
    /* TODO: more flags */
    /* TODO: timestamps */
}

static int fatdev_open(struct _reent *r, void *fileStruct, const char *path, int flags, int mode)
{
    USBHSFS_LOG("open! file: '%s'", path);
    FIL *fat_file = (FIL*)fileStruct;
    
    BYTE ff_mode = 0;
    bool has_append = false;
    switch(flags & O_ACCMODE)
    {
        case O_RDONLY:
            ff_mode = FA_READ;
            break;
        case O_WRONLY:
            has_append = true;
            ff_mode = FA_WRITE;
            break;
        case O_RDWR:
            has_append = true;
            ff_mode = FA_READ | FA_WRITE;
            break;
        default:
            r->_errno = EINVAL;
            return -1;
    }

    if (flags & O_CREAT) ff_mode |= FA_CREATE_ALWAYS;
    else if (has_append) ff_mode |= FA_OPEN_APPEND;
    else ff_mode |= FA_OPEN_EXISTING;

    USBHSFS_LOG("Flags: %d", ff_mode);
    
    FRESULT ff_res = f_open(fat_file, path, ff_mode);
    if (ff_res == FR_OK) return 0;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return -1;
}

static int fatdev_close(struct _reent *r, void *fd)
{
    FIL *fat_file = (FIL*)fd;

    FRESULT ff_res = f_close(fat_file);
    if (ff_res == FR_OK) return 0;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return -1;
}

static ssize_t fatdev_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    FIL *fat_file = (FIL*)fd;

    UINT bw;
    FRESULT ff_res = f_write(fat_file, ptr, len, &bw);
    if (ff_res == FR_OK) return (ssize_t)bw;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return -1;
}

static ssize_t fatdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    FIL *fat_file = (FIL*)fd;

    UINT br;
    FRESULT ff_res = f_read(fat_file, ptr, len, &br);
    if (ff_res == FR_OK) return (ssize_t)br;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return -1;
}

static off_t fatdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    FIL *fat_file = (FIL*)fd;

    FSIZE_t off = (FSIZE_t)pos;
    switch(dir)
    {
        case SEEK_SET:
            break;
        case SEEK_CUR:
            off += f_tell(fat_file);
            break;
        case SEEK_END:
            off += f_size(fat_file);
            break;
    }

    FRESULT ff_res = f_lseek(fat_file, off);
    if (ff_res == FR_OK) return (off_t)off;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
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
    FILINFO fil_info;
    FRESULT ff_res = f_stat(file, &fil_info);
    if (ff_res == FR_OK)
    {
        usbHsFsFatFillStat(st, &fil_info);
        return 0;
    }
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return -1;
}

static int fatdev_link(struct _reent *r, const char *existing, const char  *newLink)
{
    /* Unsupported. */
    r->_errno = EINVAL;
    return -1;
}

static int fatdev_unlink(struct _reent *r, const char *name)
{
    FRESULT ff_res = f_unlink(name);
    if (ff_res == FR_OK) return 0;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
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
    FRESULT ff_res = f_rename(oldName, newName);
    if (ff_res == FR_OK) return 0;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return -1;
}

static int fatdev_mkdir(struct _reent *r, const char *path, int mode)
{
    FRESULT ff_res = f_mkdir(path);
    if (ff_res == FR_OK) return 0;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return -1;
}

static DIR_ITER *fatdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    DIR *fat_dir = (DIR*)dirState->dirStruct;
    FRESULT ff_res = f_opendir(fat_dir, path);
    if (ff_res == FR_OK) return dirState;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return NULL;
}

static int fatdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    DIR *fat_dir = (DIR*)dirState->dirStruct;
    FRESULT ff_res = f_rewinddir(fat_dir);
    if (ff_res == FR_OK) return 0;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return -1;
}

static int fatdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    DIR *fat_dir = (DIR*)dirState->dirStruct;
    FILINFO fil_info;
    FRESULT ff_res = f_readdir(fat_dir, &fil_info);
    if (ff_res == FR_OK)
    {
        strcpy(filename, fil_info.fname);
        usbHsFsFatFillStat(filestat, &fil_info);
        return 0;
    }
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return -1;
}

static int fatdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    DIR *fat_dir = (DIR*)dirState->dirStruct;
    FILINFO fil_info;
    FRESULT ff_res = f_closedir(fat_dir);
    if (ff_res == FR_OK) return 0;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
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
    FIL *fat_file = (FIL*)fd;

    /* Set offset, then truncate. */
    FRESULT ff_res = f_lseek(fat_file, (FSIZE_t)len);
    if (ff_res == FR_OK)
    {
        ff_res = f_truncate(fat_file);
        if (ff_res == FR_OK) return 0;
    }
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return -1;
}

static int fatdev_fsync(struct _reent *r, void *fd)
{
    FIL *fat_file = (FIL*)fd;

    FRESULT ff_res = f_sync(fat_file);
    if (ff_res == FR_OK) return 0;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
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
    FRESULT ff_res = f_rmdir(name);
    if (ff_res == FR_OK) return 0;
    
    r->_errno = usbHsFsFatConvertErrorCode(ff_res);
    return -1;
}

static const devoptab_t g_fatDevoptab =
{
  .structSize   = sizeof(FIL),
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
  .dirStateSize = sizeof(DIR),
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
    return g_fatDevoptab;
}