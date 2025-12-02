/*
 * ff_dev.c
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Loosely based on fs_dev.c from libnx, et al.
 */

#include <sys/param.h>
#include <fcntl.h>

#include "../usbhsfs_manager.h"
#include "../usbhsfs_mount.h"

#include "ff.h"

/* Helper macros. */

#define FFDEV_INIT_FILE_VARS    DEVOPTAB_INIT_FILE_VARS(FFFIL)
#define FFDEV_INIT_DIR_VARS     DEVOPTAB_INIT_DIR_VARS(FFDIR)
#define FFDEV_INIT_FS_ACCESS    DEVOPTAB_DECL_FS_CTX(FATFS)

/* Function prototypes. */

static int       ffdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode);
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
static int       ffdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2]);
static long      ffdev_fpathconf(struct _reent *r, void *fd, int name);
static long      ffdev_pathconf(struct _reent *r, const char *path, int name);
static int       ffdev_symlink(struct _reent *r, const char *target, const char *linkpath);
static ssize_t   ffdev_readlink(struct _reent *r, const char *path, char *buf, size_t bufsiz);

static const char *ffdev_get_fixed_path(struct _reent *r, const char *path, const char *cwd);

static void ffdev_fill_stat(struct stat *st, const FILINFO *info);

static void ffdev_time_posix2fat(const time_t *posix_ts, WORD *out_fat_date, WORD *out_fat_time);
static void ffdev_time_fat2posix(const WORD fat_date, const WORD fat_time, time_t *out_posix_ts);

static int ffdev_translate_error(FRESULT res);

/* Global variables. */

static const devoptab_t ffdev_devoptab = {
    .name         = NULL,
    .structSize   = sizeof(FFFIL),
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
    .dirStateSize = sizeof(FFDIR),
    .diropen_r    = ffdev_diropen,
    .dirreset_r   = ffdev_dirreset,
    .dirnext_r    = ffdev_dirnext,
    .dirclose_r   = ffdev_dirclose,
    .statvfs_r    = ffdev_statvfs,
    .ftruncate_r  = ffdev_ftruncate,
    .fsync_r      = ffdev_fsync,
    .deviceData   = NULL,
    .chmod_r      = ffdev_chmod,
    .fchmod_r     = ffdev_fchmod,       ///< Not supported by FatFs.
    .rmdir_r      = ffdev_unlink,       ///< Exactly the same as unlink.
    .lstat_r      = ffdev_stat,         ///< Symlinks aren't supported, so we'll just alias lstat() to stat().
    .utimes_r     = ffdev_utimes,
    .fpathconf_r  = ffdev_fpathconf,    ///< Not supported by FatFs.
    .pathconf_r   = ffdev_pathconf,     ///< Not supported by FatFs.
    .symlink_r    = ffdev_symlink,      ///< Not supported by FatFs.
    .readlink_r   = ffdev_readlink      ///< Not supported by FatFs.
};

const devoptab_t *ffdev_get_devoptab()
{
    return &ffdev_devoptab;
}

static int ffdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode)
{
    NX_IGNORE_ARG(mode);

    BYTE ffdev_flags = 0;
    FRESULT res = FR_OK;

    FFDEV_INIT_FILE_VARS;
    FFDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!(path = ffdev_get_fixed_path(r, path, lun_fs_ctx->cwd))) DEVOPTAB_EXIT;

    /* Check access mode. */
    switch(flags & O_ACCMODE)
    {
        case O_RDONLY:  /* Read-only. Don't allow append flag. */
            if (flags & O_APPEND) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);
            ffdev_flags |= FA_READ;
            break;
        case O_WRONLY:  /* Write-only. */
            ffdev_flags |= FA_WRITE;
            break;
        case O_RDWR:    /* Read and write. */
            ffdev_flags |= (FA_READ | FA_WRITE);
            break;
        default:        /* Invalid option. */
            DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);
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

    USBHSFS_LOG_MSG("Opening file \"%s\" with flags 0x%X (0x%X) (volume \"%s:\").", path, flags, ffdev_flags, lun_fs_ctx->name);

    /* Reset file descriptor. */
    memset(file, 0, sizeof(FFFIL));

    /* Open file. */
    res = ff_open(file, fs_ctx, path, ffdev_flags);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_close(struct _reent *r, void *fd)
{
    FRESULT res = FR_OK;

    FFDEV_INIT_FILE_VARS;

    USBHSFS_LOG_MSG("Closing file from volume \"%s:\".", lun_fs_ctx->name);

    /* Close file. */
    res = ff_close(file);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(ffdev_translate_error(res));

    /* Reset file descriptor. */
    memset(file, 0, sizeof(FFFIL));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static ssize_t ffdev_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    UINT bw = 0;
    FRESULT res = FR_OK;

    FFDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!ptr || !len) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Check if the file was opened with write access. */
    if (!(file->flag & FA_WRITE)) DEVOPTAB_SET_ERROR_AND_EXIT(EBADF);

    /* Check if the append flag is enabled. */
    if ((file->flag & (FA_OPEN_APPEND & ~FA_OPEN_ALWAYS)) && !ff_eof(file))
    {
        /* Seek to EOF. */
        res = ff_lseek(file, ff_size(file));
        if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(ffdev_translate_error(res));
    }

    USBHSFS_LOG_MSG("Writing 0x%lX byte(s) at offset 0x%lX to file in volume \"%s:\".", len, ff_tell(file), lun_fs_ctx->name);

    /* Write file data. */
    res = ff_write(file, ptr, (UINT)len, &bw);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT((ssize_t)bw);
}

static ssize_t ffdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    UINT br = 0;
    FRESULT res = FR_OK;

    FFDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!ptr || !len) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Check if the file was opened with read access. */
    if (!(file->flag & FA_READ)) DEVOPTAB_SET_ERROR_AND_EXIT(EBADF);

    USBHSFS_LOG_MSG("Reading 0x%lX byte(s) at offset 0x%lX from file in volume \"%s:\".", len, ff_tell(file), lun_fs_ctx->name);

    /* Read file data. */
    res = ff_read(file, ptr, (UINT)len, &br);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT((ssize_t)br);
}

static off_t ffdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    off_t offset = 0;
    FRESULT res = FR_OK;

    FFDEV_INIT_FILE_VARS;

    /* Find the offset to seek from. */
    switch(dir)
    {
        case SEEK_SET:  /* Set absolute position relative to zero (start offset). */
            break;
        case SEEK_CUR:  /* Set position relative to the current position. */
            offset = (off_t)ff_tell(file);
            break;
        case SEEK_END:  /* Set position relative to EOF. */
            offset = (off_t)ff_size(file);
            break;
        default:        /* Invalid option. */
            DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);
    }

    /* Don't allow negative seeks beyond the beginning of the file. */
    if (pos < 0 && offset < -pos) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Calculate actual offset. */
    offset += pos;

    USBHSFS_LOG_MSG("Seeking to offset 0x%lX from file in volume \"%s:\".", offset, lun_fs_ctx->name);

    /* Perform file seek. */
    res = ff_lseek(file, (FSIZE_t)offset);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(offset);
}

static int ffdev_fstat(struct _reent *r, void *fd, struct stat *st)
{
    /*FILINFO info = {0};

    FFDEV_INIT_FILE_VARS;

    // Sanity check.
    if (!st) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    // Only fill the attr and size field, leaving the timestamp blank.
    // TODO: find a way to retrieve timestamps for an already opened file.
    info.fattrib = file->obj.attr; // I'm not sure this is correct.
    info.fsize = file->obj.objsize;

    // Fill stat info.
    ffdev_fill_stat(st, &info);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);*/

    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(fd);
    NX_IGNORE_ARG(st);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int ffdev_stat(struct _reent *r, const char *file, struct stat *st)
{
    FILINFO info = {0};
    FRESULT res = FR_OK;

    DEVOPTAB_INIT_VARS;
    FFDEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!st) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Get fixed path. */
    if (!(file = ffdev_get_fixed_path(r, file, lun_fs_ctx->cwd))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Getting file stats for \"%s\" (volume \"%s:\").", file, lun_fs_ctx->name);

    /* Get stats. */
    res = ff_stat(fs_ctx, file, &info);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(ffdev_translate_error(res));

    /* Fill stat info. */
    ffdev_fill_stat(st, &info);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_link(struct _reent *r, const char *existing, const char *newLink)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(existing);
    NX_IGNORE_ARG(newLink);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int ffdev_unlink(struct _reent *r, const char *name)
{
    FRESULT res = FR_OK;

    DEVOPTAB_INIT_VARS;
    FFDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!(name = ffdev_get_fixed_path(r, name, lun_fs_ctx->cwd))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Deleting \"%s\" (volume \"%s:\").", name, lun_fs_ctx->name);

    /* Delete file. */
    res = ff_unlink(fs_ctx, name);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_chdir(struct _reent *r, const char *name)
{
    FFDIR dir = {0};
    FRESULT res = FR_OK;
    size_t cwd_len = 0;

    DEVOPTAB_INIT_VARS;
    FFDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!(name = ffdev_get_fixed_path(r, name, lun_fs_ctx->cwd))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Changing current directory to \"%s\" (volume \"%s:\").", name, lun_fs_ctx->name);

    /* Open directory. */
    res = ff_opendir(&dir, fs_ctx, name);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(ffdev_translate_error(res));

    /* Close directory. */
    ff_closedir(&dir);

    /* Update current working directory. */
    snprintf(lun_fs_ctx->cwd, LIBUSBHSFS_MAX_PATH, "%s", name);

    cwd_len = strlen(lun_fs_ctx->cwd);
    if (lun_fs_ctx->cwd[cwd_len - 1] != '/')
    {
        lun_fs_ctx->cwd[cwd_len] = '/';
        lun_fs_ctx->cwd[cwd_len + 1] = '\0';
    }

    /* Set default devoptab device. */
    usbHsFsMountSetDefaultDevoptabDevice(lun_fs_ctx);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_rename(struct _reent *r, const char *oldName, const char *newName)
{
    char *old_path = NULL;
    FRESULT res = FR_OK;

    DEVOPTAB_INIT_VARS;
    FFDEV_INIT_FS_ACCESS;

    /* Get fixed paths. */
    /* A copy of the first fixed path is required here because a pointer to a thread-local buffer is always returned by this function. */
    if (!(oldName = ffdev_get_fixed_path(r, oldName, lun_fs_ctx->cwd))) DEVOPTAB_EXIT;

    if (!(old_path = strdup(oldName))) DEVOPTAB_SET_ERROR_AND_EXIT(ENOMEM);

    if (!(newName = ffdev_get_fixed_path(r, newName, lun_fs_ctx->cwd))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Renaming \"%s\" to \"%s\" (volume \"%s:\").", old_path, newName, lun_fs_ctx->name);

    /* Rename entry. */
    res = _ff_rename(fs_ctx, old_path, newName);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    if (old_path) free(old_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_mkdir(struct _reent *r, const char *path, int mode)
{
    NX_IGNORE_ARG(mode);

    FRESULT res = FR_OK;

    DEVOPTAB_INIT_VARS;
    FFDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!(path = ffdev_get_fixed_path(r, path, lun_fs_ctx->cwd))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Creating directory \"%s\" (volume \"%s:\").", path, lun_fs_ctx->name);

    /* Create directory. */
    res = ff_mkdir(fs_ctx, path);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static DIR_ITER *ffdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    FRESULT res = FR_OK;
    DIR_ITER *ret = NULL;

    FFDEV_INIT_DIR_VARS;
    FFDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!(path = ffdev_get_fixed_path(r, path, lun_fs_ctx->cwd))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Opening directory \"%s\" (volume \"%s:\").", path, lun_fs_ctx->name);

    /* Reset directory state. */
    memset(dir, 0, sizeof(FFDIR));

    /* Open directory. */
    res = ff_opendir(dir, fs_ctx, path);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(ffdev_translate_error(res));

    /* Update return value. */
    ret = dirState;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_PTR(ret);
}

static int ffdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    FRESULT res = FR_OK;

    FFDEV_INIT_DIR_VARS;

    USBHSFS_LOG_MSG("Resetting state for directory in volume \"%s:\".", lun_fs_ctx->name);

    /* Reset directory state. */
    res = ff_rewinddir(dir);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    FILINFO info = {0};
    FRESULT res = FR_OK;

    FFDEV_INIT_DIR_VARS;

    /* Sanity check. */
    if (!filename || !filestat) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Getting info from next directory entry in volume \"%s:\".", lun_fs_ctx->name);

    /* Read directory. */
    res = ff_readdir(dir, &info);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(ffdev_translate_error(res));

    /* Check if we haven't reached EOD. */
    /* FatFs returns an empty string if so. */
    if (info.fname[0])
    {
        /* Copy filename. */
        sprintf(filename, "%s", info.fname);

        /* Fill stat info. */
        ffdev_fill_stat(filestat, &info);
    } else {
        /* ENOENT signals EOD. */
        DEVOPTAB_SET_ERROR(ENOENT);
    }

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    FRESULT res = FR_OK;

    FFDEV_INIT_DIR_VARS;

    USBHSFS_LOG_MSG("Closing directory from volume \"%s:\".", lun_fs_ctx->name);

    /* Close directory. */
    res = ff_closedir(dir);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(ffdev_translate_error(res));

    /* Reset directory state. */
    memset(dir, 0, sizeof(FFDIR));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{
    NX_IGNORE_ARG(path);

    DWORD free_clusters = 0;
    FRESULT res = FR_OK;

    DEVOPTAB_INIT_VARS;
    FFDEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!buf) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Getting filesystem stats for volume \"%s:\".", lun_fs_ctx->name);

    /* Get volume information. */
    res = ff_getfree(fs_ctx, &free_clusters);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(ffdev_translate_error(res));

    /* Fill filesystem stats. */
    memset(buf, 0, sizeof(struct statvfs));

    buf->f_bsize = fs_ctx->ssize;                                       /* Sector size. */
    buf->f_frsize = fs_ctx->ssize;                                      /* Sector size. */
    buf->f_blocks = ((fs_ctx->n_fatent - 2) * (DWORD)fs_ctx->csize);    /* Total cluster count * cluster size in sectors. */
    buf->f_bfree = (free_clusters * (DWORD)fs_ctx->csize);              /* Free cluster count * cluster size in sectors. */
    buf->f_bavail = buf->f_bfree;                                       /* Free cluster count * cluster size in sectors. */
    buf->f_files = 0;
    buf->f_ffree = 0;
    buf->f_favail = 0;
    buf->f_fsid = lun_fs_ctx->device_id;
    buf->f_flag = (ST_NOSUID | (fs_ctx->ro_flag ? ST_RDONLY : 0));
    buf->f_namemax = FF_LFN_BUF;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_ftruncate(struct _reent *r, void *fd, off_t len)
{
    FRESULT res = FR_OK;
    FSIZE_t cur_offset = 0;
    bool restore_offset = false;

    FFDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (len < 0) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Check if the file was opened with write access. */
    if (!(file->flag & FA_WRITE)) DEVOPTAB_SET_ERROR_AND_EXIT(EBADF);

    USBHSFS_LOG_MSG("Truncating file in volume \"%s:\" to 0x%lX bytes.", lun_fs_ctx->name, len);

    /* Backup current file offset. */
    cur_offset = ff_tell(file);

    /* Seek to the provided offset. */
    res = ff_lseek(file, (FSIZE_t)len);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(ffdev_translate_error(res));

    restore_offset = true;

    /* Truncate file. */
    res = ff_truncate(file);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    /* Restore file offset (if needed). */
    if (restore_offset)
    {
        res = ff_lseek(file, cur_offset);
        if (res != FR_OK && !DEVOPTAB_IS_ERROR_SET) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));
    }

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_fsync(struct _reent *r, void *fd)
{
    FRESULT res = FR_OK;

    FFDEV_INIT_FILE_VARS;

    USBHSFS_LOG_MSG("Synchronizing data for file in volume \"%s:\".", lun_fs_ctx->name);

    /* Synchronize file. */
    res = ff_sync(file);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_chmod(struct _reent *r, const char *path, mode_t mode)
{
    FRESULT res = FR_OK;

    DEVOPTAB_INIT_VARS;
    FFDEV_INIT_FS_ACCESS;

    /* Make sure we're using a valid attribute mask. */
    if (mode & ~(AM_ARC | AM_SYS | AM_HID | AM_RDO)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Get fixed path. */
    if (!(path = ffdev_get_fixed_path(r, path, lun_fs_ctx->cwd))) DEVOPTAB_EXIT;

    /* Change element attributes. */
    res = ff_chmod(fs_ctx, path, (BYTE)mode, (BYTE)mode);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ffdev_fchmod(struct _reent *r, void *fd, mode_t mode)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(fd);
    NX_IGNORE_ARG(mode);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int ffdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2])
{
    time_t atime = 0, mtime = 0;
    FILINFO info = {0};
    FRESULT res = FR_OK;

    DEVOPTAB_INIT_VARS;
    FFDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!(filename = ffdev_get_fixed_path(r, filename, lun_fs_ctx->cwd))) DEVOPTAB_EXIT;

    /* Check if we should use the current time. */
    if (!times)
    {
        /* Get current time. */
        atime = mtime = time(NULL);
    } else {
        /* Only use full second precision from the provided timeval values. */
        atime = times[0].tv_sec;
        mtime = times[1].tv_sec;
    }

    /* Convert POSIX timestamps into FAT timestamps. */
    ffdev_time_posix2fat(&atime, &(info.acdate), &(info.actime));
    ffdev_time_posix2fat(&mtime, &(info.fdate), &(info.ftime));

    /* Change timestamp. */
    res = ff_utime(fs_ctx, filename, &info);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(ffdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static long ffdev_fpathconf(struct _reent *r, void *fd, int name)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(fd);
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static long ffdev_pathconf(struct _reent *r, const char *path, int name)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int ffdev_symlink(struct _reent *r, const char *target, const char *linkpath)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(target);
    NX_IGNORE_ARG(linkpath);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static ssize_t ffdev_readlink(struct _reent *r, const char *path, char *buf, size_t bufsiz)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(buf);
    NX_IGNORE_ARG(bufsiz);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static const char *ffdev_get_fixed_path(struct _reent *r, const char *path, const char *cwd)
{
    DEVOPTAB_INIT_ERROR_STATE;

    const u8 *p = (const u8*)path;
    ssize_t units = 0;
    u32 code = 0;

    size_t len = 0;

    char *out = __usbhsfs_dev_path_buf;
    const size_t out_sz = MAX_ELEMENTS(__usbhsfs_dev_path_buf);

    if (!r || !path || !*path || !cwd) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Input path: \"%s\".", path);

    /* Move the path pointer to the start of the actual path. */
    do {
        units = decode_utf8(&code, p);
        if (units < 0) DEVOPTAB_SET_ERROR_AND_EXIT(EILSEQ);
        p += units;
    } while(code >= ' ' && code != ':');

    /* We found a colon; p points to the actual path. */
    if (code == ':') path = (const char*)p;

    /* Make sure there are no more colons and that the remainder of the string is valid UTF-8. */
    p = (const u8*)path;

    do {
        units = decode_utf8(&code, p);

        if (units < 0) DEVOPTAB_SET_ERROR_AND_EXIT(EILSEQ);
        if (code == ':') DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

        p += units;
        len += (size_t)units;
    } while(code >= ' ');

    /* Verify fixed path length. */
    if (*path != '/') len += strlen(cwd);
    if (len >= out_sz) DEVOPTAB_SET_ERROR_AND_EXIT(ENAMETOOLONG);

    /* Generate fixed path. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    if (*path == '/')
    {
        snprintf(out, out_sz, "%s", path);
    } else {
        snprintf(out, out_sz, "%s%s", cwd, path);
    }
#pragma GCC diagnostic pop

    USBHSFS_LOG_MSG("Fixed path: \"%s\".", out);

end:
    DEVOPTAB_RETURN_PTR(out);
}

static void ffdev_fill_stat(struct stat *st, const FILINFO *info)
{
    /* Clear stat struct. */
    memset(st, 0, sizeof(struct stat));

    /* Fill stat struct. */
    st->st_nlink = 1;

    if (info->fattrib & AM_DIR)
    {
        /* We're dealing with a directory entry. */
        st->st_mode = (S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO);
    } else {
        /* We're dealing with a file entry. */
        st->st_size = (off_t)info->fsize;
        st->st_mode = (S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO);
    }

    /* Convert FAT timestamps into POSIX timestamps. */
    ffdev_time_fat2posix(info->acdate, info->actime, &(st->st_atim.tv_sec));
    ffdev_time_fat2posix(info->fdate, info->ftime, &(st->st_mtim.tv_sec));
    ffdev_time_fat2posix(info->crdate, info->crtime, &(st->st_ctim.tv_sec));

    /* Store FAT-specific file flags. */
    st->st_spare4[0] = (long)info->fattrib;
}

static void ffdev_time_posix2fat(const time_t *posix_ts, WORD *out_fat_date, WORD *out_fat_time)
{
    if (!posix_ts || !out_fat_date || !out_fat_time) return;

    struct tm timeinfo = {0};

    /* Convert POSIX timestamp to calendar time. */
    localtime_r(posix_ts, &timeinfo);

    /* Generate full FAT timestamp. */
    DWORD fat_ts = FAT_TIMESTAMP(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    /* Store 16-bit segments into output variables. */
    *out_fat_date = (WORD)(fat_ts >> 16);
    *out_fat_time = (WORD)fat_ts;

    USBHSFS_LOG_MSG("Converted POSIX timestamp %lu (%u-%02u-%02u %02u:%02u:%02u) into DOS timestamp 0x%04X%04X.", *posix_ts, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, \
                                                                                                                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, *out_fat_date, \
                                                                                                                  *out_fat_time);
}

static void ffdev_time_fat2posix(const WORD fat_date, const WORD fat_time, time_t *out_posix_ts)
{
    if (!out_posix_ts) return;

    struct tm timeinfo = {0};

    /* Convert date/time into an actual UTC POSIX timestamp using the system local time. */
    timeinfo.tm_year = (((fat_date >> 9) & 0x7F) + 80); /* DOS time: offset since 1980. POSIX time: offset since 1900. */
    timeinfo.tm_mon = (((fat_date >> 5) & 0xF) - 1);    /* DOS time: 1-12 range (inclusive). POSIX time: 0-11 range (inclusive). */
    timeinfo.tm_mday = (fat_date & 0x1F);
    timeinfo.tm_hour = ((fat_time >> 11) & 0x1F);
    timeinfo.tm_min = ((fat_time >> 5) & 0x3F);
    timeinfo.tm_sec = ((fat_time & 0x1F) << 1);         /* DOS time: 2-second intervals with a 0-29 range (inclusive, 58 seconds max). POSIX time: 0-59 range (inclusive). */

    *out_posix_ts = mktime(&timeinfo);

    USBHSFS_LOG_MSG("Converted DOS timestamp 0x%04X%04X (%u-%02u-%02u %02u:%02u:%02u) into POSIX timestamp %lu.", fat_date, fat_time, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, \
                                                                                                                  timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, \
                                                                                                                  *out_posix_ts);
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

    USBHSFS_LOG_MSG("FRESULT: %u. Translated errno: %d.", res, ret);

    return ret;
}
