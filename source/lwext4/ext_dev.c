/*
 * ext_dev.c
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Loosely based on fs_dev.c from libnx, et al.
 */

#include <sys/param.h>
#include <fcntl.h>

#include "../usbhsfs_manager.h"
#include "../usbhsfs_mount.h"

#include "ext.h"

/* Helper macros. */

#define EXTDEV_INIT_FILE_VARS  DEVOPTAB_INIT_FILE_VARS(ext4_file)
#define EXTDEV_INIT_DIR_VARS   DEVOPTAB_INIT_DIR_VARS(ext4_dir)
#define EXTDEV_INIT_FS_ACCESS  DEVOPTAB_DECL_FS_CTX(ext_vd)

/* Function prototypes. */

static int       extdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode);
static int       extdev_close(struct _reent *r, void *fd);
static ssize_t   extdev_write(struct _reent *r, void *fd, const char *ptr, size_t len);
static ssize_t   extdev_read(struct _reent *r, void *fd, char *ptr, size_t len);
static off_t     extdev_seek(struct _reent *r, void *fd, off_t pos, int dir);
static int       extdev_fstat(struct _reent *r, void *fd, struct stat *st);
static int       extdev_stat(struct _reent *r, const char *file, struct stat *st);
static int       extdev_link(struct _reent *r, const char *existing, const char *newLink);
static int       extdev_unlink(struct _reent *r, const char *name);
static int       extdev_chdir(struct _reent *r, const char *name);
static int       extdev_rename(struct _reent *r, const char *oldName, const char *newName);
static int       extdev_mkdir(struct _reent *r, const char *path, int mode);
static DIR_ITER* extdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path);
static int       extdev_dirreset(struct _reent *r, DIR_ITER *dirState);
static int       extdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat);
static int       extdev_dirclose(struct _reent *r, DIR_ITER *dirState);
static int       extdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf);
static int       extdev_ftruncate(struct _reent *r, void *fd, off_t len);
static int       extdev_fsync(struct _reent *r, void *fd);
static int       extdev_chmod(struct _reent *r, const char *path, mode_t mode);
static int       extdev_fchmod(struct _reent *r, void *fd, mode_t mode);
static int       extdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2]);
static long      extdev_fpathconf(struct _reent *r, void *fd, int name);
static long      extdev_pathconf(struct _reent *r, const char *path, int name);
static int       extdev_symlink(struct _reent *r, const char *target, const char *linkpath);
static ssize_t   extdev_readlink(struct _reent *r, const char *path, char *buf, size_t bufsiz);

static const char *extdev_get_fixed_path(struct _reent *r, const char *path, UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx);

static void extdev_fill_stat(const struct ext4_inode *inode, u32 st_dev, u32 st_ino, u32 st_blksize, struct stat *st);

static int ext_trans_start(struct ext4_fs *ext_fs);
static int ext_trans_stop(struct ext4_fs *ext_fs);
static void ext_trans_abort(struct ext4_fs *ext_fs);

/* Global variables. */

static const devoptab_t extdev_devoptab = {
    .name         = NULL,
    .structSize   = sizeof(ext4_file),
    .open_r       = extdev_open,
    .close_r      = extdev_close,
    .write_r      = extdev_write,
    .read_r       = extdev_read,
    .seek_r       = extdev_seek,
    .fstat_r      = extdev_fstat,
    .stat_r       = extdev_stat,
    .link_r       = extdev_link,
    .unlink_r     = extdev_unlink,
    .chdir_r      = extdev_chdir,
    .rename_r     = extdev_rename,
    .mkdir_r      = extdev_mkdir,
    .dirStateSize = sizeof(ext4_dir),
    .diropen_r    = extdev_diropen,
    .dirreset_r   = extdev_dirreset,
    .dirnext_r    = extdev_dirnext,
    .dirclose_r   = extdev_dirclose,
    .statvfs_r    = extdev_statvfs,
    .ftruncate_r  = extdev_ftruncate,
    .fsync_r      = extdev_fsync,       ///< Not supported by lwext4.
    .deviceData   = NULL,
    .chmod_r      = extdev_chmod,
    .fchmod_r     = extdev_fchmod,
    .rmdir_r      = extdev_unlink,      ///< Exactly the same as unlink.
    .lstat_r      = extdev_stat,        ///< We'll just alias lstat() to stat().
    .utimes_r     = extdev_utimes,
    .fpathconf_r  = extdev_fpathconf,   ///< Not implemented.
    .pathconf_r   = extdev_pathconf,    ///< Not implemented.
    .symlink_r    = extdev_symlink,     ///< Not implemented.
    .readlink_r   = extdev_readlink     ///< Not implemented.
};

const devoptab_t *extdev_get_devoptab()
{
    return &extdev_devoptab;
}

static int extdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode)
{
    NX_IGNORE_ARG(mode);

    int ret = -1;

    EXTDEV_INIT_FILE_VARS;

    /* Get fixed path. */
    if (!(path = extdev_get_fixed_path(r, path, lun_fs_ctx))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Opening file \"%s\" with flags 0x%X.", path, flags);

    /* Reset file descriptor. */
    memset(file, 0, sizeof(ext4_file));

    /* Open file. */
    ret = ext4_fopen2(file, path, flags);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_close(struct _reent *r, void *fd)
{
    int ret = -1;

    EXTDEV_INIT_FILE_VARS;

    USBHSFS_LOG_MSG("Closing file %u.", file->inode);

    /* Close file. */
    ret = ext4_fclose(file);
    if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);

    /* Reset file descriptor. */
    memset(file, 0, sizeof(ext4_file));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static ssize_t extdev_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    size_t bw = 0;
    int ret = -1;

    EXTDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!ptr || !len) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Check if the append flag is enabled. */
    if ((file->flags & O_APPEND) && ext4_ftell(file) != ext4_fsize(file))
    {
        /* Seek to EOF. */
        ret = ext4_fseek(file, 0, SEEK_END);
        if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);
    }

    USBHSFS_LOG_MSG("Writing 0x%lX byte(s) to file %u at offset 0x%lX.", len, file->inode, ext4_ftell(file));

    /* Write file data. */
    ret = ext4_fwrite(file, ptr, len, &bw);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT((ssize_t)bw);
}

static ssize_t extdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    size_t br = 0;
    int ret = -1;

    EXTDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!ptr || !len) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Reading 0x%lX byte(s) from file %u at offset 0x%lX.", len, file->inode, ext4_ftell(file));

    /* Read file data. */
    ret = ext4_fread(file, ptr, len, &br);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT((ssize_t)br);
}

static off_t extdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    off_t offset = 0;
    int ret = -1;

    EXTDEV_INIT_FILE_VARS;

    USBHSFS_LOG_MSG("Seeking 0x%lX byte(s) from position %d in file %u.", pos, dir, file->inode);

    /* Perform file seek. */
    ret = ext4_fseek(file, (s64)pos, (u32)dir);
    if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);

    /* Update current offset. */
    offset = (off_t)ext4_ftell(file);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(offset);
}

static int extdev_fstat(struct _reent *r, void *fd, struct stat *st)
{
    struct ext4_inode_ref inode_ref = {0};
    int ret = -1;

    EXTDEV_INIT_FILE_VARS;
    EXTDEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!st) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Get inode reference. */
    ret = ext4_fs_get_inode_ref(fs_ctx->bdev->fs, file->inode, &inode_ref);
    if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);

    /* Fill stat info. */
    extdev_fill_stat(inode_ref.inode, lun_fs_ctx->device_id, file->inode, fs_ctx->bdev->lg_bsize, st);

    /* Put back inode reference. */
    ret = ext4_fs_put_inode_ref(&inode_ref);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_stat(struct _reent *r, const char *file, struct stat *st)
{
    u32 inode_num = 0;
    struct ext4_inode inode = {0};
    int ret = -1;

    DEVOPTAB_INIT_VARS;
    EXTDEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!st) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Get fixed path. */
    if (!(file = extdev_get_fixed_path(r, file, lun_fs_ctx))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Getting stats for \"%s\".", file);

    /* Get inode. */
    ret = ext4_raw_inode_fill(file, &inode_num, &inode);
    if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);

    /* Fill stat info. */
    extdev_fill_stat(&inode, lun_fs_ctx->device_id, inode_num, fs_ctx->bdev->lg_bsize, st);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_link(struct _reent *r, const char *existing, const char *newLink)
{
    char *existing_path = NULL;
    int ret = -1;

    DEVOPTAB_INIT_VARS;

    /* Get fixed paths. */
    /* A copy of the first fixed path is required here because a pointer to a thread-local buffer is always returned by this function. */
    if (!(existing = extdev_get_fixed_path(r, existing, lun_fs_ctx))) DEVOPTAB_EXIT;

    if (!(existing_path = strdup(existing))) DEVOPTAB_SET_ERROR_AND_EXIT(ENOMEM);

    if (!(newLink = extdev_get_fixed_path(r, newLink, lun_fs_ctx))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Linking \"%s\" to \"%s\".", existing_path, newLink);

    /* Create hard link. */
    ret = ext4_flink(existing_path, newLink);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    if (existing_path) free(existing_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_unlink(struct _reent *r, const char *name)
{
    int ret = -1;

    DEVOPTAB_INIT_VARS;

    /* Get fixed path. */
    if (!(name = extdev_get_fixed_path(r, name, lun_fs_ctx))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Deleting \"%s\".", name);

    /* Delete file. */
    ret = ext4_fremove(name);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_chdir(struct _reent *r, const char *name)
{
    ext4_dir dir = {0};
    size_t cwd_len = 0;
    int ret = -1;

    DEVOPTAB_INIT_VARS;

    /* Get fixed path. */
    if (!(name = extdev_get_fixed_path(r, name, lun_fs_ctx))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Changing current directory to \"%s\".", name);

    /* Open directory. */
    ret = ext4_dir_open(&dir, name);
    if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);

    /* Close directory. */
    ext4_dir_close(&dir);

    /* Update current working directory. */
    snprintf(lun_fs_ctx->cwd, LIBUSBHSFS_MAX_PATH, "%s", strchr(name + 1, '/'));

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

static int extdev_rename(struct _reent *r, const char *oldName, const char *newName)
{
    char *old_path = NULL;
    int ret = -1;

    DEVOPTAB_INIT_VARS;

    /* Get fixed paths. */
    /* A copy of the first fixed path is required here because a pointer to a thread-local buffer is always returned by this function. */
    if (!(oldName = extdev_get_fixed_path(r, oldName, lun_fs_ctx))) DEVOPTAB_EXIT;

    if (!(old_path = strdup(oldName))) DEVOPTAB_SET_ERROR_AND_EXIT(ENOMEM);

    if (!(newName = extdev_get_fixed_path(r, newName, lun_fs_ctx))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Renaming \"%s\" to \"%s\".", old_path, newName);

    /* Rename entry. */
    ret = ext4_frename(old_path, newName);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    if (old_path) free(old_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_mkdir(struct _reent *r, const char *path, int mode)
{
    NX_IGNORE_ARG(mode);

    int ret = -1;

    DEVOPTAB_INIT_VARS;

    /* Get fixed path. */
    if (!(path = extdev_get_fixed_path(r, path, lun_fs_ctx))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Creating directory \"%s\".", path);

    /* Create directory. */
    ret = ext4_dir_mk(path);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static DIR_ITER *extdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    int res = -1;
    DIR_ITER *ret = NULL;

    EXTDEV_INIT_DIR_VARS;

    /* Get fixed path. */
    if (!(path = extdev_get_fixed_path(r, path, lun_fs_ctx))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Opening directory \"%s\".", path);

    /* Reset directory state. */
    memset(dir, 0, sizeof(ext4_dir));

    /* Open directory. */
    res = ext4_dir_open(dir, path);
    if (res) DEVOPTAB_SET_ERROR_AND_EXIT(res);

    /* Update return value. */
    ret = dirState;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_PTR(ret);
}

static int extdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    EXTDEV_INIT_DIR_VARS;

    USBHSFS_LOG_MSG("Resetting state from directory %u.", dir->f.inode);

    /* Reset directory state. */
    ext4_dir_entry_rewind(dir);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    const ext4_direntry *entry = NULL;
    struct ext4_inode_ref inode_ref = {0};
    int ret = -1;

    EXTDEV_INIT_DIR_VARS;
    EXTDEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!filename || !filestat) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Getting info from next entry in directory %u.", dir->f.inode);

    while(true)
    {
        /* Read directory. */
        entry = ext4_dir_entry_next(dir);
        if (!entry) break;

        /* Filter unsupported entry types. */
        if (entry->inode_type != EXT4_DE_REG_FILE && entry->inode_type != EXT4_DE_DIR && entry->inode_type != EXT4_DE_SYMLINK) continue;

        /* Filter dot directory entries. */
        //if (entry->inode_type == EXT4_DE_DIR && (!strcmp((char*)entry->name, ".") || !strcmp((char*)entry->name, ".."))) continue;

        /* Jackpot. */
        break;
    }

    if (!entry) DEVOPTAB_SET_ERROR_AND_EXIT(ENOENT); /* ENOENT signals EOD. */

    /* Get inode reference. */
    ret = ext4_fs_get_inode_ref(fs_ctx->bdev->fs, entry->inode, &inode_ref);
    if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);

    /* Copy filename. */
    sprintf(filename, "%.*s", (int)entry->name_length, (char*)entry->name);

    /* Fill stat info. */
    extdev_fill_stat(inode_ref.inode, lun_fs_ctx->device_id, entry->inode, fs_ctx->bdev->lg_bsize, filestat);

    /* Put back inode reference. */
    ret = ext4_fs_put_inode_ref(&inode_ref);
    if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    int ret = -1;

    EXTDEV_INIT_DIR_VARS;

    USBHSFS_LOG_MSG("Closing directory %u.", dir->f.inode);

    /* Close directory. */
    ret = ext4_dir_close(dir);
    if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);

    /* Reset directory state. */
    memset(dir, 0, sizeof(ext4_dir));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{
    NX_IGNORE_ARG(path);

    char mount_point[CONFIG_EXT4_MAX_MP_NAME + 3] = {0};
    struct ext4_mount_stats mount_stats = {0};
    int ret = -1;

    DEVOPTAB_INIT_VARS;
    EXTDEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!buf) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Generate lwext4 mount point. */
    sprintf(mount_point, "/%s/", fs_ctx->dev_name);

    USBHSFS_LOG_MSG("Getting filesystem stats for \"%s\" (\"%s\").", path, mount_point);

    /* Get volume information. */
    ret = ext4_mount_point_stats(mount_point, &mount_stats);
    if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);

    /* Fill filesystem stats. */
    memset(buf, 0, sizeof(struct statvfs));

    buf->f_bsize = mount_stats.block_size;
    buf->f_frsize = mount_stats.block_size;
    buf->f_blocks = mount_stats.blocks_count;
    buf->f_bfree = mount_stats.free_blocks_count;
    buf->f_bavail = mount_stats.free_blocks_count;
    buf->f_files = mount_stats.inodes_count;
    buf->f_ffree = mount_stats.free_inodes_count;
    buf->f_favail = mount_stats.free_inodes_count;
    buf->f_fsid = lun_fs_ctx->device_id;
    buf->f_flag = (ST_NOSUID | (((fs_ctx->flags & UsbHsFsMountFlags_ReadOnly) || lun_ctx->write_protect) ? ST_RDONLY : 0));
    buf->f_namemax = EXT4_DIRECTORY_FILENAME_LEN;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_ftruncate(struct _reent *r, void *fd, off_t len)
{
    int ret = -1;

    EXTDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (len < 0) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Truncating file %u to 0x%lX bytes.", file->inode, len);

    /* Truncate file. */
    ret = ext4_ftruncate(file, (u64)len);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_fsync(struct _reent *r, void *fd)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(fd);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int extdev_chmod(struct _reent *r, const char *path, mode_t mode)
{
    int ret = -1;

    DEVOPTAB_INIT_VARS;

    /* Get fixed path. */
    if (!(path = extdev_get_fixed_path(r, path, lun_fs_ctx))) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Changing permissions for \"%s\" to %o.", path, mode);

    /* Change permissions. */
    ret = ext4_mode_set(path, (u32)mode);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_fchmod(struct _reent *r, void *fd, mode_t mode)
{
    struct ext4_fs *ext_fs = NULL;
    struct ext4_sblock *sblock = NULL;
    struct ext4_inode_ref inode_ref = {0};
    u32 orig_mode = 0;
    int ret = -1;

    EXTDEV_INIT_FILE_VARS;
    EXTDEV_INIT_FS_ACCESS;

    ext_fs = fs_ctx->bdev->fs;
    sblock = &(fs_ctx->bdev->fs->sb);

    /* Start journal transfer. */
    ret = ext_trans_start(ext_fs);
    if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);

    /* Get inode reference. */
    ret = ext4_fs_get_inode_ref(ext_fs, file->inode, &inode_ref);
    if (ret)
    {
        ext_trans_abort(ext_fs);
        DEVOPTAB_SET_ERROR_AND_EXIT(ret);
    }

    USBHSFS_LOG_MSG("Changing permissions for file %u to %o.", file->inode, mode);

    /* Change permissions. */
	orig_mode = ext4_inode_get_mode(sblock, inode_ref.inode);
	orig_mode &= ~0xFFF;
	orig_mode |= (mode & 0xFFF);
	ext4_inode_set_mode(sblock, inode_ref.inode, orig_mode);
    inode_ref.dirty = true;

    /* Put back inode reference. */
    ret = ext4_fs_put_inode_ref(&inode_ref);
    if (ret)
    {
        ext_trans_abort(ext_fs);
        DEVOPTAB_SET_ERROR_AND_EXIT(ret);
    }

    /* Stop journal transfer. */
    ret = ext_trans_stop(ext_fs);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int extdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2])
{
    time_t atime = 0, mtime = 0;
    int ret = -1;

    DEVOPTAB_INIT_VARS;

    /* Get fixed path. */
    if (!(filename = extdev_get_fixed_path(r, filename, lun_fs_ctx))) DEVOPTAB_EXIT;

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

    USBHSFS_LOG_MSG("Setting last access and modification times for \"%s\" to 0x%lX and 0x%lX, respectively.", filename, atime, mtime);

    /* Set access time. */
    ret = ext4_atime_set(filename, (u32)atime);
    if (ret) DEVOPTAB_SET_ERROR_AND_EXIT(ret);

    /* Set modification time. */
    ret = ext4_mtime_set(filename, (u32)mtime);
    if (ret) DEVOPTAB_SET_ERROR(ret);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static long extdev_fpathconf(struct _reent *r, void *fd, int name)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(fd);
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static long extdev_pathconf(struct _reent *r, const char *path, int name)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int extdev_symlink(struct _reent *r, const char *target, const char *linkpath)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(target);
    NX_IGNORE_ARG(linkpath);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static ssize_t extdev_readlink(struct _reent *r, const char *path, char *buf, size_t bufsiz)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(buf);
    NX_IGNORE_ARG(bufsiz);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static const char *extdev_get_fixed_path(struct _reent *r, const char *path, UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx)
{
    DEVOPTAB_INIT_ERROR_STATE;

    const u8 *p = (const u8*)path;
    ssize_t units = 0;
    u32 code = 0;

    size_t len = 0;
    const char *cwd = NULL;

    char mount_point[CONFIG_EXT4_MAX_MP_NAME + 3] = {0};

    char *out = __usbhsfs_dev_path_buf;
    const size_t out_sz = MAX_ELEMENTS(__usbhsfs_dev_path_buf);

    if (!r || !path || !*path || !lun_fs_ctx) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    EXTDEV_INIT_FS_ACCESS;

    if (!(cwd = lun_fs_ctx->cwd) || !fs_ctx) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Input path: \"%s\".", path);

    /* Generate lwext4 mount point. */
    sprintf(mount_point, "/%s", fs_ctx->dev_name);

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
    len = strlen(mount_point);

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
        snprintf(out, out_sz, "%s%s", mount_point, path);
    } else {
        snprintf(out, out_sz, "%s%s%s", mount_point, cwd, path);
    }
#pragma GCC diagnostic pop

    USBHSFS_LOG_MSG("Fixed path: \"%s\".", out);

end:
    DEVOPTAB_RETURN_PTR(out);
}

static void extdev_fill_stat(const struct ext4_inode *inode, u32 st_dev, u32 st_ino, u32 st_blksize, struct stat *st)
{
    /* Clear stat struct. */
    memset(st, 0, sizeof(struct stat));

    /* Fill stat struct. */
    st->st_dev = st_dev;
    st->st_ino = st_ino;
    st->st_mode = inode->mode;
    st->st_nlink = inode->links_count;
    st->st_uid = inode->uid;
    st->st_gid = inode->gid;
    if (inode->mode & (EXT4_INODE_MODE_FILE | EXT4_INODE_MODE_SOFTLINK)) st->st_size = ((((u64)inode->size_hi << 0x20) & 0xFFFFFFFF00000000ULL) | (u64)inode->size_lo);
    st->st_blksize = st_blksize;
    st->st_blocks = inode->blocks_count_lo;

    st->st_atim.tv_sec = inode->access_time;
    st->st_atim.tv_nsec = inode->atime_extra;

    st->st_mtim.tv_sec = inode->modification_time;
    st->st_mtim.tv_nsec = inode->mtime_extra;

    st->st_ctim.tv_sec = inode->crtime;
    st->st_ctim.tv_nsec = inode->crtime_extra;
}

static int ext_trans_start(struct ext4_fs *ext_fs)
{
    struct jbd_journal *journal = NULL;
    struct jbd_trans *trans = NULL;
    int ret = 0;

    if (ext_fs->jbd_journal && !ext_fs->curr_trans)
    {
        journal = ext_fs->jbd_journal;

        trans = jbd_journal_new_trans(journal);
        if (!trans)
        {
            ret = ENOMEM;
            goto end;
        }

        ext_fs->curr_trans = trans;
    }

end:
    return ret;
}

static int ext_trans_stop(struct ext4_fs *ext_fs)
{
    struct jbd_journal *journal = NULL;
    struct jbd_trans *trans = NULL;
    int ret = 0;

    if (ext_fs->jbd_journal && ext_fs->curr_trans)
    {
        journal = ext_fs->jbd_journal;
        trans = ext_fs->curr_trans;
        ret = jbd_journal_commit_trans(journal, trans);
        ext_fs->curr_trans = NULL;
    }

    return ret;
}

static void ext_trans_abort(struct ext4_fs *ext_fs)
{
    struct jbd_journal *journal = NULL;
    struct jbd_trans *trans = NULL;

    if (ext_fs->jbd_journal && ext_fs->curr_trans)
    {
        journal = ext_fs->jbd_journal;
        trans = ext_fs->curr_trans;
        jbd_journal_free_trans(journal, trans, true);
        ext_fs->curr_trans = NULL;
    }
}
