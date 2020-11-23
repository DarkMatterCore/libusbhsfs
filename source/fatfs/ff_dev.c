/*
 * ff_dev.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Loosely based on fs_dev.c from libnx, et al.
 */

#include <sys/param.h>
#include <fcntl.h>

#include "ff_dev.h"

/* Function prototypes. */

static UsbHsFsDriveContext *ffdev_get_drive_ctx_and_lock(UsbHsFsDriveLogicalUnitFileSystemContext **fs_ctx);

static bool ffdev_fixpath(struct _reent *r, const char *path, UsbHsFsDriveLogicalUnitFileSystemContext **fs_ctx, char *outpath);

static void ffdev_fill_stat(struct stat *st, const FILINFO *info);

static int ffdev_translate_error(FRESULT res);

static int       ffdev_open(struct _reent *r, void *fileStruct, const char *path, int flags, int mode);
static int       ffdev_close(struct _reent *r, void *fd);
static ssize_t   ffdev_write(struct _reent *r, void *fd, const char *ptr, size_t len);
static ssize_t   ffdev_read(struct _reent *r, void *fd, char *ptr, size_t len);
static off_t     ffdev_seek(struct _reent *r, void *fd, off_t pos, int dir);
static int       ffdev_fstat(struct _reent *r, void *fd, struct stat *st);
static int       ffdev_stat(struct _reent *r, const char *file, struct stat *st);
static int       ffdev_link(struct _reent *r, const char *existing, const char *newLink);
static int       ffdev_unlink(struct _reent *r, const char *name);
static int       ffdev_chdir(struct _reent *r, const char *name);
static int       ffdev_rename(struct _reent *r, const char *oldName, const char *newName);
static int       ffdev_mkdir(struct _reent *r, const char *path, int mode);
static DIR_ITER* ffdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path);
static int       ffdev_dirreset(struct _reent *r, DIR_ITER *dirState);
static int       ffdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat);
static int       ffdev_dirclose(struct _reent *r, DIR_ITER *dirState);
static int       ffdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf);
static int       ffdev_ftruncate(struct _reent *r, void *fd, off_t len);
static int       ffdev_fsync(struct _reent *r, void *fd);
static int       ffdev_chmod(struct _reent *r, const char *path, mode_t mode);
static int       ffdev_fchmod(struct _reent *r, void *fd, mode_t mode);
static int       ffdev_rmdir(struct _reent *r, const char *name);
static int       ffdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2]);

/* Global variables. */

static __thread char ffdev_path_buf[USB_MAX_PATH_LENGTH] = {0};

static const devoptab_t ffdev_devoptab = {
    .name         = NULL,
    .structSize   = sizeof(FIL),
    .open_r       = ffdev_open,
    .close_r      = ffdev_close,
    .write_r      = ffdev_write,
    .read_r       = ffdev_read,
    .seek_r       = ffdev_seek,
    .fstat_r      = ffdev_fstat,        ///< Not supported by FatFs.
    .stat_r       = ffdev_stat,
    .link_r       = ffdev_link,         ///< Not supported by FatFs.
    .unlink_r     = ffdev_unlink,
    .chdir_r      = ffdev_chdir,
    .rename_r     = ffdev_rename,
    .mkdir_r      = ffdev_mkdir,
    .dirStateSize = sizeof(DIR),
    .diropen_r    = ffdev_diropen,
    .dirreset_r   = ffdev_dirreset,
    .dirnext_r    = ffdev_dirnext,
    .dirclose_r   = ffdev_dirclose,
    .statvfs_r    = ffdev_statvfs,
    .ftruncate_r  = ffdev_ftruncate,
    .fsync_r      = ffdev_fsync,
    .deviceData   = NULL,
    .chmod_r      = ffdev_chmod,        ///< Not supported by FatFs.
    .fchmod_r     = ffdev_fchmod,       ///< Not supported by FatFs.
    .rmdir_r      = ffdev_rmdir,
    .lstat_r      = ffdev_stat,         ///< Symlinks aren't supported, so we'll just alias lstat() to stat().
    .utimes_r     = ffdev_utimes
};

const devoptab_t *ffdev_get_devoptab()
{
    return &ffdev_devoptab;
}

static UsbHsFsDriveContext *ffdev_get_drive_ctx_and_lock(UsbHsFsDriveLogicalUnitFileSystemContext **fs_ctx)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = NULL;
    UsbHsFsDriveContext *drive_ctx = NULL;
    
    /* Lock drive manager mutex. */
    usbHsFsManagerMutexControl(true);
    
    /* Check if we have a valid filesystem context pointer. */
    if (fs_ctx && *fs_ctx)
    {
        /* Get pointer to the LUN context. */
        lun_ctx = (UsbHsFsDriveLogicalUnitContext*)(*fs_ctx)->lun_ctx;
        
        /* Get pointer to the drive context and lock its mutex. */
        drive_ctx = usbHsFsManagerGetDriveContextForLogicalUnitContext(lun_ctx);
        if (drive_ctx) mutexLock(&(drive_ctx->mutex));
    }
    
    /* Unlock drive manager mutex. */
    usbHsFsManagerMutexControl(false);
    
    return drive_ctx;
}

static bool ffdev_fixpath(struct _reent *r, const char *path, UsbHsFsDriveLogicalUnitFileSystemContext **fs_ctx, char *outpath)
{
    FATFS *fatfs = NULL;
    
    if (!r || !path || !*path || !fs_ctx || !*fs_ctx || !(fatfs = (*fs_ctx)->fatfs))
    {
        r->_errno = EINVAL;
        return false;
    }
    
    ssize_t units = 0;
    u32 code = 0;
    const u8 *p = (const u8*)path;
    size_t len = 0;
    char name[USB_MOUNT_NAME_LENGTH] = {0}, *outptr = (outpath ? outpath : ffdev_path_buf), *cwd = (*fs_ctx)->cwd;
    
    USBHSFS_LOG("Input path: \"%s\".", path);
    
    /* Generate FatFs mount name ID. */
    sprintf(name, "%u:", fatfs->pdrv);
    
    /* Move the path pointer to the start of the actual path. */
    do {
        units = decode_utf8(&code, p);
        if (units < 0)
        {
            r->_errno = EILSEQ;
            return false;
        }
        
        p += units;
    } while(code != ':' && code != 0);
    
    /* We found a colon; p points to the actual path. */
    if (code == ':') path = (const char*)p;
    
    /* Make sure there are no more colons and that the remainder of the filename is valid UTF-8. */
    p = (const uint8_t*)path;
    do {
        units = decode_utf8(&code, p);
        if (units < 0)
        {
            r->_errno = EILSEQ;
            return false;
        }
        
        if (code == ':')
        {
            r->_errno = EINVAL;
            return false;
        }
        
        p += units;
    } while(code != 0);
    
    /* Verify fixed path length. */
    len = (strlen(name) + strlen(path));
    if (path[0] != '/') len += strlen(cwd);
    
    if (len >= USB_MAX_PATH_LENGTH)
    {
        r->_errno = ENAMETOOLONG;
        return false;
    }
    
    /* Generate fixed path. */
    if (path[0] == '/')
    {
        sprintf(outptr, "%s%s", name, path);
    } else {
        sprintf(outptr, "%s%s%s", name, cwd, path);
    }
    
    USBHSFS_LOG("Fixed path: \"%s\".", outptr);
    
    return true;
}

static void ffdev_fill_stat(struct stat *st, const FILINFO *info)
{
    struct tm timeinfo = {0};
    
    /* Clear stat struct. */
    memset(st, 0, sizeof(struct stat));
    
    /* Fill stat struct. */
    st->st_nlink = 1;
    
    if (info->fattrib & AM_DIR)
    {
        st->st_mode = (S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO);
    } else {
        st->st_size = (off_t)info->fsize;
        st->st_mode = (S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    }
    
    /* Convert date/time into an actual UTC POSIX timestamp using the system local time. */
    timeinfo.tm_year = (((info->fdate >> 9) & 0x7F) + 80);  /* DOS time: offset since 1980. POSIX time: offset since 1900. */
    timeinfo.tm_mon = (((info->fdate >> 5) & 0xF) - 1);     /* DOS time: 1-12 range (inclusive). POSIX time: 0-11 range (inclusive). */
    timeinfo.tm_mday = (info->fdate & 0x1F);
    timeinfo.tm_hour = ((info->ftime >> 11) & 0x1F);
    timeinfo.tm_min = ((info->ftime >> 5) & 0x3F);
    timeinfo.tm_sec = ((info->ftime & 0x1F) << 1);          /* DOS time: 2-second intervals with a 0-29 range (inclusive, 58 seconds max). POSIX time: 0-59 range (inclusive). */
    
    st->st_atime = 0;                   /* Not returned by FatFs + only available under exFAT. */
    st->st_mtime = mktime(&timeinfo);
    st->st_ctime = 0;                   /* Not returned by FatFs + only available under exFAT. */
    
    USBHSFS_LOG("DOS timestamp: 0x%04X%04X. Generated POSIX timestamp: %lu.", info->fdate, info->ftime, st->st_mtime);
}

static int ffdev_translate_error(FRESULT res)
{
    int ret;
    
    switch(res)
    {
        case FR_OK:
            ret = 0;
            break;
        case FR_DISK_ERR:
        case FR_NOT_READY:
            ret = EIO;
            break;
        case FR_INT_ERR:
        case FR_INVALID_NAME:
        case FR_INVALID_PARAMETER:
            ret = EINVAL;
            break;
        case FR_NO_FILE:
        case FR_NO_PATH:
            ret = ENOENT;
            break;
        case FR_DENIED:
            ret = EACCES;
            break;
        case FR_EXIST:
            ret = EEXIST;
            break;
        case FR_INVALID_OBJECT:
            ret = EFAULT;
            break;
        case FR_WRITE_PROTECTED:
            ret = EROFS;
            break;
        case FR_INVALID_DRIVE:
            ret = ENODEV;
            break;
        case FR_NOT_ENABLED:
        case FR_MKFS_ABORTED:
            ret = ENOEXEC;
            break;
        case FR_NO_FILESYSTEM:
            ret = ENFILE;
            break;
        case FR_TIMEOUT:
            ret = EAGAIN;
            break;
        case FR_LOCKED:
            ret = EBUSY;
            break;
        case FR_NOT_ENOUGH_CORE:
            ret = ENOMEM;
            break;
        case FR_TOO_MANY_OPEN_FILES:
            ret = EMFILE;
            break;
        default:
            ret = EPERM;
            break;
    }
    
    USBHSFS_LOG("FRESULT: %u. Translated errno: %d.", res, ret);
    
    return ret;
}

static int ffdev_open(struct _reent *r, void *fileStruct, const char *path, int flags, int mode)
{
    FIL *file = (FIL*)fileStruct;
    BYTE ffdev_flags = 0;
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Fix input path. */
    if (!ffdev_fixpath(r, path, &fs_ctx, NULL)) goto end;
    
    /* Check access mode. */
    switch(flags & O_ACCMODE)
    {
        case O_RDONLY:  /* Read-only. Don't allow append flag. */
            if (flags & O_APPEND)
            {
                r->_errno = EINVAL;
                goto end;
            }
            
            ffdev_flags |= FA_READ;
            break;
        case O_WRONLY:  /* Write-only. */
            ffdev_flags |= FA_WRITE;
            break;
        case O_RDWR:    /* Read and write. */
            ffdev_flags |= (FA_READ | FA_WRITE);
            break;
        default:        /* Invalid option. */
            r->_errno = EINVAL;
            goto end;
    }
    
    if ((flags & O_ACCMODE) != O_RDONLY)
    {
        if (flags & O_TRUNC)
        {
            /* Create a new file. If the file exists, it will be truncated and overwritten. */
            ffdev_flags |= FA_CREATE_ALWAYS;
        } else
        if (flags & O_CREAT)
        {
            /* O_EXCL set: create a new file. Fail if the file already exists. */
            /* O_EXCL cleared: */
            /*     - O_APPEND set: open file. If it doesn't exist, it will be created. The file pointer will be set to EOF before each write. */
            /*     - O_APPEND cleared: open file. If it doesn't exist, it will be created. */
            ffdev_flags |= ((flags & O_EXCL) ? FA_CREATE_NEW : ((flags & O_APPEND) ? FA_OPEN_APPEND : FA_OPEN_ALWAYS));
        } else {
            /* Open file. Fail if the file doesn't exist. */
            ffdev_flags |= FA_OPEN_EXISTING;
        }
    } else {
        /* Open file. Fail if the file doesn't exist. */
        ffdev_flags |= FA_OPEN_EXISTING;
    }
    
    USBHSFS_LOG("Opening file \"%s\" (\"%s\") with flags 0x%X (0x%X).", path, ffdev_path_buf, flags, ffdev_flags);
    
    /* Open file. */
    res = f_open(file, ffdev_path_buf, ffdev_flags);
    if (res == FR_OK)
    {
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_close(struct _reent *r, void *fd)
{
    FIL *file = (FIL*)fd;
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    USBHSFS_LOG("Closing file from \"%u:\".", file->obj.fs->pdrv);
    
    /* Close file. */
    res = f_close(file);
    if (res == FR_OK)
    {
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static ssize_t ffdev_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    FIL *file = (FIL*)fd;
    UINT bw = 0;
    FRESULT res = FR_OK;
    ssize_t ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Check if the file was opened with write access. */
    if (!(file->flag & FA_WRITE))
    {
        r->_errno = EBADF;
        goto end;
    }
    
    /* Check if the append flag is enabled. */
    if ((file->flag & (FA_OPEN_APPEND & ~FA_OPEN_ALWAYS)) && !f_eof(file))
    {
        res = f_lseek(file, f_size(file));
        if (res != FR_OK)
        {
            r->_errno = ffdev_translate_error(res);
            goto end;
        }
    }
    
    USBHSFS_LOG("Writing 0x%lX byte(s) to file in \"%u:\" at offset 0x%lX.", len, file->obj.fs->pdrv, f_tell(file));
    
    /* Write file data. */
    res = f_write(file, ptr, (UINT)len, &bw);
    if (res == FR_OK)
    {
        ret = (ssize_t)bw;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static ssize_t ffdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    FIL *file = (FIL*)fd;
    UINT br = 0;
    FRESULT res = FR_OK;
    ssize_t ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Check if the file was opened with read access. */
    if (!(file->flag & FA_READ))
    {
        r->_errno = EBADF;
        goto end;
    }
    
    USBHSFS_LOG("Reading 0x%lX byte(s) from file in \"%u:\" at offset 0x%lX.", len, file->obj.fs->pdrv, f_tell(file));
    
    /* Read file data. */
    res = f_read(file, ptr, (UINT)len, &br);
    if (res == FR_OK)
    {
        ret = (ssize_t)br;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static off_t ffdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    FIL *file = (FIL*)fd;
    s64 offset = 0;
    FRESULT res = FR_OK;
    off_t ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Find the offset to seek from. */
    switch(dir)
    {
        case SEEK_SET:  /* Set absolute position relative to zero (start offset). */
            break;
        case SEEK_CUR:  /* Set position relative to the current position. */
            offset = (s64)f_tell(file);
            break;
        case SEEK_END:  /* Set position relative to EOF. */
            offset = (s64)f_size(file);
            break;
        default:        /* Invalid option. */
            r->_errno = EINVAL;
            goto end;
    }
    
    /* Don't allow negative seeks beyond the beginning of the file. */
    if (pos < 0 && offset < -pos)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Calculate actual offset. */
    offset += pos;
    
    USBHSFS_LOG("Seeking to offset 0x%lX from file in \"%u:\".", offset, file->obj.fs->pdrv, f_tell(file));
    
    /* Perform file seek. */
    res = f_lseek(file, (FSIZE_t)offset);
    if (res == FR_OK)
    {
        ret = (off_t)offset;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_fstat(struct _reent *r, void *fd, struct stat *st)
{
    /* Not supported by FatFs. */
    r->_errno = ENOSYS;
    return -1;
}

static int ffdev_stat(struct _reent *r, const char *file, struct stat *st)
{
    FILINFO info = {0};
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Fix input path. */
    if (!ffdev_fixpath(r, file, &fs_ctx, NULL)) goto end;
    
    USBHSFS_LOG("Getting file stats for \"%s\" (\"%s\").", file, ffdev_path_buf);
    
    /* Get file stats. */
    res = f_stat(ffdev_path_buf, &info);
    if (res == FR_OK)
    {
        /* Fill stat info. */
        ffdev_fill_stat(st, &info);
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_link(struct _reent *r, const char *existing, const char *newLink)
{
    /* Not supported by FatFs. */
    r->_errno = ENOSYS;
    return -1;
}

static int ffdev_unlink(struct _reent *r, const char *name)
{
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Fix input path. */
    if (!ffdev_fixpath(r, name, &fs_ctx, NULL)) goto end;
    
    USBHSFS_LOG("Deleting \"%s\" (\"%s\").", name, ffdev_path_buf);
    
    /* Delete file. */
    res = f_unlink(ffdev_path_buf);
    if (res == FR_OK)
    {
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_chdir(struct _reent *r, const char *name)
{
    FRESULT res = FR_OK;
    size_t cwd_len = 0;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Fix input path. */
    if (!ffdev_fixpath(r, name, &fs_ctx, NULL)) goto end;
    
    USBHSFS_LOG("Changing current directory to \"%s\" (\"%s\").", name, ffdev_path_buf);
    
    /* Change directory. */
    res = f_chdir(ffdev_path_buf);
    if (res == FR_OK)
    {
        /* Update current working directory. */
        sprintf(fs_ctx->cwd, "%s", strchr(ffdev_path_buf, '/'));
        
        cwd_len = strlen(fs_ctx->cwd);
        if (fs_ctx->cwd[cwd_len - 1] != '/')
        {
            fs_ctx->cwd[cwd_len] = '/';
            fs_ctx->cwd[cwd_len + 1] = '\0';
        }
        
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_rename(struct _reent *r, const char *oldName, const char *newName)
{
    char old_path[USB_MAX_PATH_LENGTH] = {0};
    char *new_path = ffdev_path_buf;
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Fix input paths. */
    if (!ffdev_fixpath(r, oldName, &fs_ctx, old_path) || !ffdev_fixpath(r, newName, &fs_ctx, new_path)) goto end;
    
    USBHSFS_LOG("Renaming \"%s\" (\"%s\") to \"%s\" (\"%s\").", oldName, old_path, newName, new_path);
    
    /* Rename entry. */
    res = f_rename(old_path, new_path);
    if (res == FR_OK)
    {
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_mkdir(struct _reent *r, const char *path, int mode)
{
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Fix input path. */
    if (!ffdev_fixpath(r, path, &fs_ctx, NULL)) goto end;
    
    USBHSFS_LOG("Creating directory \"%s\" (\"%s\").", path, ffdev_path_buf);
    
    /* Create directory. */
    res = f_mkdir(ffdev_path_buf);
    if (res == FR_OK)
    {
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static DIR_ITER *ffdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    DIR *dir = (DIR*)dirState->dirStruct;
    FRESULT res = FR_OK;
    DIR_ITER *ret = NULL;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Fix input path. */
    if (!ffdev_fixpath(r, path, &fs_ctx, NULL)) goto end;
    
    USBHSFS_LOG("Opening directory \"%s\" (\"%s\").", path, ffdev_path_buf);
    
    /* Open directory. */
    res = f_opendir(dir, ffdev_path_buf);
    if (res == FR_OK)
    {
        ret = dirState;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    DIR *dir = (DIR*)dirState->dirStruct;
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    USBHSFS_LOG("Resetting directory state from \"%u:\".", dir->obj.fs->pdrv);
    
    /* Reset directory state. */
    res = f_rewinddir(dir);
    if (res == FR_OK)
    {
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    DIR *dir = (DIR*)dirState->dirStruct;
    FILINFO info = {0};
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    USBHSFS_LOG("Getting info from next directory entry in \"%u:\".", dir->obj.fs->pdrv);
    
    /* Read directory. */
    res = f_readdir(dir, &info);
    if (res == FR_OK)
    {
        /* Check if we haven't reached EOD. */
        /* FatFs returns an empty string if so. */
        if (info.fname[0])
        {
            /* Copy filename. */
            strcpy(filename, info.fname);
            
            /* Fill stat info. */
            ffdev_fill_stat(filestat, &info);
            ret = 0;
        } else {
            /* ENOENT signals EOD. */
            r->_errno = ENOENT;
        }
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    DIR *dir = (DIR*)dirState->dirStruct;
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    USBHSFS_LOG("Closing directory from \"%u:\".", dir->obj.fs->pdrv);
    
    /* Close directory. */
    res = f_closedir(dir);
    if (res == FR_OK)
    {
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{
    char name[USB_MOUNT_NAME_LENGTH] = {0};
    DWORD free_clusters = 0;
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Generate volume name. */
    FATFS *fatfs = fs_ctx->fatfs;
    sprintf(name, "%u:", fatfs->pdrv);
    
    USBHSFS_LOG("Getting filesystem stats for \"%s\" (\"%s\").", path, name);
    
    /* Get volume information. */
    res = f_getfree(name, &free_clusters, &fatfs);
    if (res == FR_OK)
    {
        /* Fill filesystem stats. */
        memset(buf, 0, sizeof(struct statvfs));
        
        buf->f_bsize = fatfs->ssize;                                    /* Sector size. */
        buf->f_frsize = fatfs->ssize;                                   /* Sector size. */
        buf->f_blocks = ((fatfs->n_fatent - 2) * (DWORD)fatfs->csize);  /* Total cluster count * cluster size in sectors. */
        buf->f_bfree = (free_clusters * (DWORD)fatfs->csize);           /* Free cluster count * cluster size in sectors. */
        buf->f_bavail = buf->f_bfree;                                     /* Free cluster count * cluster size in sectors. */
        buf->f_files = 0;
        buf->f_ffree = 0;
        buf->f_favail = 0;
        buf->f_fsid = 0;
        buf->f_flag = ST_NOSUID;
        buf->f_namemax = FF_LFN_BUF;
        
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_ftruncate(struct _reent *r, void *fd, off_t len)
{
    FIL *file = (FIL*)fd;
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Make sure length is non-negative. */
    if (len < 0)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    USBHSFS_LOG("Truncating file in \"%u:\" to 0x%lX bytes.", file->obj.fs->pdrv, len);
    
    /* Seek to the provided offset. */
    res = f_lseek(file, (FSIZE_t)len);
    if (res == FR_OK)
    {
        /* Truncate file. */
        res = f_truncate(file);
        if (res == FR_OK) ret = 0;
    }
    
    if (res != FR_OK) r->_errno = ffdev_translate_error(res);
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_fsync(struct _reent *r, void *fd)
{
    FIL *file = (FIL*)fd;
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    USBHSFS_LOG("Synchronizing data for file in \"%u:\".", file->obj.fs->pdrv);
    
    /* Synchronize file data. */
    res = f_sync(file);
    if (res == FR_OK)
    {
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

static int ffdev_chmod(struct _reent *r, const char *path, mode_t mode)
{
    /* Not supported by FatFs. */
    r->_errno = ENOSYS;
    return -1;
}

static int ffdev_fchmod(struct _reent *r, void *fd, mode_t mode)
{
    /* Not supported by FatFs. */
    r->_errno = ENOSYS;
    return -1;
}

static int ffdev_rmdir(struct _reent *r, const char *name)
{
    /* Exactly the same as ffdev_unlink(). */
    return ffdev_unlink(r, name);
}

static int ffdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2])
{
    Result rc = 0;
    time_t mtime = times[1].tv_sec; /* Only modify the last modification date and time. */
    TimeCalendarTime caltime = {0};
    DWORD timestamp = 0;
    
    FILINFO info = {0};
    FRESULT res = FR_OK;
    int ret = -1;
    
    /* Get drive context and lock its mutex. */
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData;
    UsbHsFsDriveContext *drive_ctx = ffdev_get_drive_ctx_and_lock(&fs_ctx);
    if (!drive_ctx)
    {
        r->_errno = EINVAL;
        goto end;
    }
    
    /* Fix input path. */
    if (!ffdev_fixpath(r, filename, &fs_ctx, NULL)) goto end;
    
    /* Convert POSIX timestamp to calendar time. */
    rc = timeToCalendarTimeWithMyRule((u64)mtime, &caltime, NULL);
    if (R_SUCCEEDED(rc))
    {
        /* Generate FAT timestamp. */
        timestamp = FAT_TIMESTAMP(caltime.year, caltime.month, caltime.day, caltime.hour, caltime.minute, caltime.second);
        
        /* Fill FILINFO time data. */
        info.fdate = (WORD)(timestamp >> 16);
        info.ftime = (WORD)(timestamp & 0xFF);
    }
    
    USBHSFS_LOG("Setting last modification time for \"%s\" (\"%s\") to %u-%02u-%02u %02u:%02u:%02u (0x%04X%04X).", filename, ffdev_path_buf, caltime.year, caltime.month, caltime.day, caltime.hour, \
                caltime.minute, caltime.second, info.fdate, info.ftime);
    
    /* Change timestamp. */
    res = f_utime(ffdev_path_buf, &info);
    if (res == FR_OK)
    {
        ret = 0;
    } else {
        r->_errno = ffdev_translate_error(res);
    }
    
end:
    /* Unlock drive mutex. */
    if (drive_ctx) mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}
