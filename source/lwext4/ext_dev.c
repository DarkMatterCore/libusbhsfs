/*
 * ext_dev.c
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Loosely based on fs_dev.c from libnx, et al.
 */

#include <sys/param.h>
#include <fcntl.h>

#include "../usbhsfs_manager.h"
#include "../usbhsfs_mount.h"

/* Helper macros. */

#define ext_end                     goto end
#define ext_ended_with_error        (_errno != 0)
#define ext_set_error(x)            r->_errno = _errno = (x)
#define ext_set_error_and_exit(x)   \
do { \
    ext_set_error((x)); \
    ext_end; \
} while(0)

#define ext_declare_error_state     int _errno = 0
#define ext_declare_file_state      ext4_file *file = (ext4_file*)fd
#define ext_declare_dir_state       ext4_dir *dir = (ext4_dir*)dirState->dirStruct
#define ext_declare_fs_ctx          UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData
#define ext_declare_lun_ctx         UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)fs_ctx->lun_ctx
#define ext_declare_drive_ctx       UsbHsFsDriveContext *drive_ctx = (UsbHsFsDriveContext*)lun_ctx->drive_ctx
#define ext_declare_vol_state       ext_vd *vd = fs_ctx->ext

#define ext_lock_drive_ctx          ext_declare_fs_ctx; \
                                    ext_declare_lun_ctx; \
                                    ext_declare_drive_ctx; \
                                    bool drive_ctx_valid = usbHsFsManagerIsDriveContextPointerValid(drive_ctx); \
                                    if (!drive_ctx_valid) ext_set_error_and_exit(ENODEV)

#define ext_unlock_drive_ctx        if (drive_ctx_valid) mutexUnlock(&(drive_ctx->mutex))

#define ext_return(x)               return (ext_ended_with_error ? -1 : (x))
#define ext_return_ptr(x)           return (ext_ended_with_error ? NULL : (x))
#define ext_return_bool             return (ext_ended_with_error ? false : true)

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
static int       extdev_rmdir(struct _reent *r, const char *name);
static int       extdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2]);

static bool extdev_fixpath(struct _reent *r, const char *path, UsbHsFsDriveLogicalUnitFileSystemContext **fs_ctx, char *outpath);

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
    .rmdir_r      = extdev_rmdir,
    .lstat_r      = extdev_stat,        ///< We'll just alias lstat() to stat().
    .utimes_r     = extdev_utimes
};

const devoptab_t *extdev_get_devoptab()
{
    return &extdev_devoptab;
}

static int extdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode)
{
    (void)mode;
    
    int ret = -1;
    
    ext_declare_error_state;
    ext_declare_file_state;
    ext_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file) ext_set_error_and_exit(EINVAL);
    
    /* Fix input path. */
    if (!extdev_fixpath(r, path, &fs_ctx, NULL)) ext_end;
    
    USBHSFS_LOG("Opening file \"%s\" (\"%s\") with flags 0x%X.", path, __usbhsfs_dev_path_buf, flags);
    
    /* Reset file descriptor. */
    memset(file, 0, sizeof(ext4_file));
    
    /* Open file. */
    ret = ext4_fopen2(file, __usbhsfs_dev_path_buf, flags);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_close(struct _reent *r, void *fd)
{
    int ret = -1;
    
    ext_declare_error_state;
    ext_declare_file_state;
    ext_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file) ext_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Closing file %u.", file->inode);
    
    /* Close file. */
    ret = ext4_fclose(file);
    if (ret) ext_set_error_and_exit(ret);
    
    /* Reset file descriptor. */
    memset(file, 0, sizeof(ext4_file));
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static ssize_t extdev_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    size_t bw = 0;
    int ret = -1;
    
    ext_declare_error_state;
    ext_declare_file_state;
    ext_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file || !ptr || !len) ext_set_error_and_exit(EINVAL);
    
    /* Check if the append flag is enabled. */
    if ((file->flags & O_APPEND) && ext4_ftell(file) != ext4_fsize(file))
    {
        /* Seek to EOF. */
        ret = ext4_fseek(file, 0, SEEK_END);
        if (ret) ext_set_error_and_exit(ret);
    }
    
    USBHSFS_LOG("Writing 0x%lX byte(s) to file %u at offset 0x%lX.", len, file->inode, ext4_ftell(file));
    
    /* Write file data. */
    ret = ext4_fwrite(file, ptr, len, &bw);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return((ssize_t)bw);
}

static ssize_t extdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    size_t br = 0;
    int ret = -1;
    
    ext_declare_error_state;
    ext_declare_file_state;
    ext_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file || !ptr || !len) ext_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Reading 0x%lX byte(s) from file %u at offset 0x%lX.", len, file->inode, ext4_ftell(file));
    
    /* Read file data. */
    ret = ext4_fread(file, ptr, len, &br);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return((ssize_t)br);
}

static off_t extdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    off_t offset = 0;
    int ret = -1;
    
    ext_declare_error_state;
    ext_declare_file_state;
    ext_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file) ext_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Seeking 0x%lX byte(s) from current position in file %u.", pos, file->inode);
    
    /* Perform file seek. */
    ret = ext4_fseek(file, (s64)pos, (u32)dir);
    if (ret) ext_set_error_and_exit(ret);
    
    /* Update current offset. */
    offset = (off_t)ext4_ftell(file);
    
end:
    ext_unlock_drive_ctx;
    ext_return(offset);
}

static int extdev_fstat(struct _reent *r, void *fd, struct stat *st)
{
    struct ext4_inode_ref inode_ref = {0};
    int ret = -1;
    
    ext_declare_error_state;
    ext_declare_file_state;
    ext_lock_drive_ctx;
    ext_declare_vol_state;
    
    /* Sanity check. */
    if (!file || !st) ext_set_error_and_exit(EINVAL);
    
    /* Get inode reference. */
    ret = ext4_fs_get_inode_ref(vd->bdev->fs, file->inode, &inode_ref);
    if (ret) ext_set_error_and_exit(ret);
    
    /* Fill stat info. */
    extdev_fill_stat(inode_ref.inode, fs_ctx->device_id, file->inode, vd->bdev->lg_bsize, st);
    
    /* Put back inode reference. */
    ret = ext4_fs_put_inode_ref(&inode_ref);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_stat(struct _reent *r, const char *file, struct stat *st)
{
    u32 inode_num = 0;
    struct ext4_inode inode = {0};
    int ret = -1;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    ext_declare_vol_state;
    
    /* Sanity check. */
    if (!st) ext_set_error_and_exit(EINVAL);
    
    /* Fix input path. */
    if (!extdev_fixpath(r, file, &fs_ctx, NULL)) ext_end;
    
    USBHSFS_LOG("Getting stats for \"%s\" (\"%s\").", file, __usbhsfs_dev_path_buf);
    
    /* Get inode. */
    ret = ext4_raw_inode_fill(__usbhsfs_dev_path_buf, &inode_num, &inode);
    if (ret) ext_set_error_and_exit(ret);
    
    /* Fill stat info. */
    extdev_fill_stat(&inode, fs_ctx->device_id, inode_num, vd->bdev->lg_bsize, st);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_link(struct _reent *r, const char *existing, const char *newLink)
{
    char existing_path[USB_MAX_PATH_LENGTH] = {0};
    char *new_path = __usbhsfs_dev_path_buf;
    int ret = -1;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    
    /* Fix input paths. */
    if (!extdev_fixpath(r, existing, &fs_ctx, existing_path) || !extdev_fixpath(r, newLink, &fs_ctx, new_path)) ext_end;
    
    USBHSFS_LOG("Linking \"%s\" (\"%s\") to \"%s\" (\"%s\").", existing, existing_path, newLink, new_path);
    
    /* Create hard link. */
    ret = ext4_flink(existing_path, new_path);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_unlink(struct _reent *r, const char *name)
{
    int ret = -1;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    
    /* Fix input path. */
    if (!extdev_fixpath(r, name, &fs_ctx, NULL)) ext_end;
    
    USBHSFS_LOG("Deleting \"%s\" (\"%s\").", name, __usbhsfs_dev_path_buf);
    
    /* Delete file. */
    ret = ext4_fremove(__usbhsfs_dev_path_buf);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_chdir(struct _reent *r, const char *name)
{
    ext4_dir dir = {0};
    size_t cwd_len = 0;
    int ret = -1;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    
    /* Fix input path. */
    if (!extdev_fixpath(r, name, &fs_ctx, NULL)) ext_end;
    
    USBHSFS_LOG("Changing current directory to \"%s\" (\"%s\").", name, __usbhsfs_dev_path_buf);
    
    /* Open directory. */
    ret = ext4_dir_open(&dir, __usbhsfs_dev_path_buf);
    if (ret) ext_set_error_and_exit(ret);
    
    /* Close directory. */
    ext4_dir_close(&dir);
    
    /* Update current working directory. */
    sprintf(fs_ctx->cwd, "%s", strchr(__usbhsfs_dev_path_buf + 1, '/'));
    
    cwd_len = strlen(fs_ctx->cwd);
    if (fs_ctx->cwd[cwd_len - 1] != '/')
    {
        fs_ctx->cwd[cwd_len] = '/';
        fs_ctx->cwd[cwd_len + 1] = '\0';
    }
    
    /* Set default devoptab device. */
    usbHsFsMountSetDefaultDevoptabDevice(fs_ctx);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_rename(struct _reent *r, const char *oldName, const char *newName)
{
    char old_path[USB_MAX_PATH_LENGTH] = {0};
    char *new_path = __usbhsfs_dev_path_buf;
    int ret = -1;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    
    /* Fix input paths. */
    if (!extdev_fixpath(r, oldName, &fs_ctx, old_path) || !extdev_fixpath(r, newName, &fs_ctx, new_path)) ext_end;
    
    USBHSFS_LOG("Renaming \"%s\" (\"%s\") to \"%s\" (\"%s\").", oldName, old_path, newName, new_path);
    
    /* Rename entry. */
    ret = ext4_frename(old_path, new_path);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_mkdir(struct _reent *r, const char *path, int mode)
{
    (void)mode;
    
    int ret = -1;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    
    /* Fix input path. */
    if (!extdev_fixpath(r, path, &fs_ctx, NULL)) ext_end;
    
    USBHSFS_LOG("Creating directory \"%s\" (\"%s\").", path, __usbhsfs_dev_path_buf);
    
    /* Create directory. */
    ret = ext4_dir_mk(__usbhsfs_dev_path_buf);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static DIR_ITER *extdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    int res = -1;
    DIR_ITER *ret = NULL;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    
    /* Sanity check. */
    if (!dirState) ext_set_error_and_exit(EINVAL);
    
    ext_declare_dir_state;
    
    /* Fix input path. */
    if (!extdev_fixpath(r, path, &fs_ctx, NULL)) ext_end;
    
    USBHSFS_LOG("Opening directory \"%s\" (\"%s\").", path, __usbhsfs_dev_path_buf);
    
    /* Reset directory state. */
    memset(dir, 0, sizeof(ext4_dir));
    
    /* Open directory. */
    res = ext4_dir_open(dir, __usbhsfs_dev_path_buf);
    if (res) ext_set_error_and_exit(res);
    
    /* Update return value. */
    ret = dirState;
    
end:
    ext_unlock_drive_ctx;
    ext_return_ptr(ret);
}

static int extdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    ext_declare_error_state;
    ext_lock_drive_ctx;
    
    /* Sanity check. */
    if (!dirState) ext_set_error_and_exit(EINVAL);
    
    ext_declare_dir_state;
    
    USBHSFS_LOG("Resetting state from directory %u.", dir->f.inode);
    
    /* Reset directory state. */
    ext4_dir_entry_rewind(dir);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    const ext4_direntry *entry = NULL;
    struct ext4_inode_ref inode_ref = {0};
    int ret = -1;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    ext_declare_vol_state;
    
    /* Sanity check. */
    if (!dirState || !filename || !filestat) ext_set_error_and_exit(EINVAL);
    
    ext_declare_dir_state;
    
    USBHSFS_LOG("Getting info from next entry in directory %u.", dir->f.inode);
    
    while(true)
    {
        /* Read directory. */
        entry = ext4_dir_entry_next(dir);
        if (!entry) break;
        
        /* Filter entry types. */
        if (entry->inode_type != EXT4_DE_REG_FILE && entry->inode_type != EXT4_DE_DIR && entry->inode_type != EXT4_DE_SYMLINK) continue;
        
        /* Filter dot directory entries. */
        if (entry->inode_type == EXT4_DE_DIR && (!strcmp((char*)entry->name, ".") || !strcmp((char*)entry->name, ".."))) continue;
        
        /* Jackpot. */
        break;
    }
    
    if (!entry) ext_set_error_and_exit(ENOENT); /* ENOENT signals EOD. */
    
    /* Get inode reference. */
    ret = ext4_fs_get_inode_ref(vd->bdev->fs, entry->inode, &inode_ref);
    if (ret) ext_set_error_and_exit(ret);
    
    /* Fill stat info. */
    extdev_fill_stat(inode_ref.inode, fs_ctx->device_id, entry->inode, vd->bdev->lg_bsize, filestat);
    
    /* Put back inode reference. */
    ret = ext4_fs_put_inode_ref(&inode_ref);
    if (ret) ext_set_error_and_exit(ret);
    
    /* Copy filename. */
    strcpy(filename, (char*)entry->name);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    int ret = -1;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    
    /* Sanity check. */
    if (!dirState) ext_set_error_and_exit(EINVAL);
    
    ext_declare_dir_state;
    
    USBHSFS_LOG("Closing directory %u.", dir->f.inode);
    
    /* Close directory. */
    ret = ext4_dir_close(dir);
    if (ret) ext_set_error_and_exit(ret);
    
    /* Reset directory state. */
    memset(dir, 0, sizeof(ext4_dir));
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{
    (void)path;
    
    char mount_point[CONFIG_EXT4_MAX_MP_NAME + 3] = {0};
    struct ext4_mount_stats mount_stats = {0};
    int ret = -1;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    ext_declare_vol_state;
    
    /* Sanity check. */
    if (!buf) ext_set_error_and_exit(EINVAL);
    
    /* Generate lwext4 mount point. */
    sprintf(mount_point, "/%s/", vd->dev_name);
    
    USBHSFS_LOG("Getting filesystem stats for \"%s\" (\"%s\").", path, mount_point);
    
    /* Get volume information. */
    ret = ext4_mount_point_stats(mount_point, &mount_stats);
    if (ret) ext_set_error_and_exit(ret);
    
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
    buf->f_fsid = fs_ctx->device_id;
    buf->f_flag = ST_NOSUID;
    buf->f_namemax = EXT4_DIRECTORY_FILENAME_LEN;
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_ftruncate(struct _reent *r, void *fd, off_t len)
{
    int ret = -1;
    
    ext_declare_error_state;
    ext_declare_file_state;
    ext_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file || len < 0) ext_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Truncating file %u to 0x%lX bytes.", file->inode, len);
    
    /* Truncate file. */
    ret = ext4_ftruncate(file, (u64)len);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_fsync(struct _reent *r, void *fd)
{
    (void)fd;
    
    /* Not supported by lwext4. */
    r->_errno = ENOSYS;
    return -1;
}

static int extdev_chmod(struct _reent *r, const char *path, mode_t mode)
{
    int ret = -1;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    
    /* Fix input path. */
    if (!extdev_fixpath(r, path, &fs_ctx, NULL)) ext_end;
    
    USBHSFS_LOG("Changing permissions for \"%s\" (\"%s\") to %o.", path, __usbhsfs_dev_path_buf, mode);
    
    /* Change permissions. */
    ret = ext4_mode_set(__usbhsfs_dev_path_buf, (u32)mode);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_fchmod(struct _reent *r, void *fd, mode_t mode)
{
    struct ext4_fs *ext_fs = NULL;
    struct ext4_sblock *sblock = NULL;
    struct ext4_inode_ref inode_ref = {0};
    u32 orig_mode = 0;
    int ret = -1;
    
    ext_declare_error_state;
    ext_declare_file_state;
    ext_lock_drive_ctx;
    ext_declare_vol_state;
    
    ext_fs = vd->bdev->fs;
    sblock = &(vd->bdev->fs->sb);
    
    /* Sanity check. */
    if (!file) ext_set_error_and_exit(EINVAL);
    
    /* Start journal transfer. */
    ret = ext_trans_start(ext_fs);
    if (ret) ext_set_error_and_exit(ret);
    
    /* Get inode reference. */
    ret = ext4_fs_get_inode_ref(ext_fs, file->inode, &inode_ref);
    if (ret)
    {
        ext_trans_abort(ext_fs);
        ext_set_error_and_exit(ret);
    }
    
    USBHSFS_LOG("Changing permissions for file %u to %o.", file->inode, mode);
    
    /* Change permissions. */
	orig_mode = ext4_inode_get_mode(sblock, inode_ref.inode);
	orig_mode &= ~0xFFF;
	orig_mode |= ((u32)mode & 0xFFF);
	ext4_inode_set_mode(sblock, inode_ref.inode, orig_mode);
    inode_ref.dirty = true;
    
    /* Put back inode reference. */
    ret = ext4_fs_put_inode_ref(&inode_ref);
    if (ret)
    {
        ext_trans_abort(ext_fs);
        ext_set_error_and_exit(ret);
    }
    
    /* Stop journal transfer. */
    ret = ext_trans_stop(ext_fs);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static int extdev_rmdir(struct _reent *r, const char *name)
{
    /* Exactly the same as extdev_unlink(). */
    return extdev_unlink(r, name);
}

static int extdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2])
{
    struct timespec ts_times[2] = {0};
    int ret = -1;
    
    ext_declare_error_state;
    ext_lock_drive_ctx;
    
    /* Fix input path. */
    if (!extdev_fixpath(r, filename, &fs_ctx, NULL)) ext_end;
    
    /* Check if we should use the current time. */
    if (!times)
    {
        /* Get current time. */
        clock_gettime(CLOCK_REALTIME, &(ts_times[0]));
        memcpy(&(ts_times[1]), &(ts_times[0]), sizeof(struct timespec));
    } else {
        /* Convert provided timeval values to timespec values. */
        TIMEVAL_TO_TIMESPEC(&(times[0]), &(ts_times[0]));
        TIMEVAL_TO_TIMESPEC(&(times[1]), &(ts_times[1]));
    }
    
    USBHSFS_LOG("Setting last access and modification times for \"%s\" (\"%s\") to 0x%lX and 0x%lX, respectively.", filename, __usbhsfs_dev_path_buf, ts_times[0].tv_sec, ts_times[1].tv_sec);
    
    /* Set access time. */
    ret = ext4_atime_set(__usbhsfs_dev_path_buf, (u32)ts_times[0].tv_sec);
    if (ret) ext_set_error_and_exit(ret);
    
    /* Set modification time. */
    ret = ext4_mtime_set(__usbhsfs_dev_path_buf, (u32)ts_times[1].tv_sec);
    if (ret) ext_set_error(ret);
    
end:
    ext_unlock_drive_ctx;
    ext_return(0);
}

static bool extdev_fixpath(struct _reent *r, const char *path, UsbHsFsDriveLogicalUnitFileSystemContext **fs_ctx, char *outpath)
{
    ext_vd *vd = NULL;
    const u8 *p = (const u8*)path;
    ssize_t units = 0;
    u32 code = 0;
    size_t len = 0;
    char mount_point[CONFIG_EXT4_MAX_MP_NAME + 3] = {0}, *outptr = (outpath ? outpath : __usbhsfs_dev_path_buf), *cwd = NULL;
    
    ext_declare_error_state;
    
    if (!r || !path || !*path || !fs_ctx || !*fs_ctx || !(vd = (*fs_ctx)->ext) || !(cwd = (*fs_ctx)->cwd)) ext_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Input path: \"%s\".", path);
    
    /* Generate lwext4 mount point. */
    sprintf(mount_point, "/%s", vd->dev_name);
    
    /* Move the path pointer to the start of the actual path. */
    do {
        units = decode_utf8(&code, p);
        if (units < 0) ext_set_error_and_exit(EILSEQ);
        p += units;
    } while(code >= ' ' && code != ':');
    
    /* We found a colon; p points to the actual path. */
    if (code == ':') path = (const char*)p;
    
    /* Make sure there are no more colons and that the remainder of the string is valid UTF-8. */
    p = (const u8*)path;
    
    do {
        units = decode_utf8(&code, p);
        if (units < 0) ext_set_error_and_exit(EILSEQ);
        if (code == ':') ext_set_error_and_exit(EINVAL);
        p += units;
    } while(code >= ' ');
    
    /* Verify fixed path length. */
    len = (strlen(mount_point) + strlen(path));
    if (path[0] != '/') len += strlen(cwd);
    
    if (len >= USB_MAX_PATH_LENGTH) ext_set_error_and_exit(ENAMETOOLONG);
    
    /* Generate fixed path. */
    if (path[0] == '/')
    {
        sprintf(outptr, "%s%s", mount_point, path);
    } else {
        sprintf(outptr, "%s%s%s", mount_point, cwd, path);
    }
    
    USBHSFS_LOG("Fixed path: \"%s\".", outptr);
    
end:
    ext_return_bool;
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
