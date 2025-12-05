/*
 * ntfs_dev.c
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii). Also loosely based on fs_dev.c from libnx, et al.
 */

#include <sys/param.h>
#include <fcntl.h>

#include "../usbhsfs_manager.h"
#include "../usbhsfs_mount.h"
#include "ntfs.h"

/* Helper macros. */

#define NTFSDEV_INIT_FILE_VARS  DEVOPTAB_INIT_FILE_VARS(ntfs_file_state)
#define NTFSDEV_INIT_DIR_VARS   DEVOPTAB_INIT_DIR_VARS(ntfs_dir_state)
#define NTFSDEV_INIT_FS_ACCESS  DEVOPTAB_DECL_FS_CTX(ntfs_vd)

/* Type definitions. */

/// NTFS file state.
typedef struct _ntfs_file_state {
    ntfs_vd *vd;        ///< File volume descriptor.
    ntfs_inode *ni;     ///< File node descriptor.
    ntfs_attr *data;    ///< File data attribute descriptor.
    int flags;          ///< File open flags.
    bool read;          ///< True if allowed to read from file.
    bool write;         ///< True if allowed to write to file.
    bool append;        ///< True if allowed to append to file.
    bool compressed;    ///< True if file data is compressed.
    bool encrypted;     ///< True if file data is encryted.
    off_t pos;          ///< Current position within the file (in bytes).
    u64 len;            ///< Total file length (in bytes).
} ntfs_file_state;

/// NTFS directory entry.
typedef struct _ntfs_dir_entry {
    u64 mref;                       ///< Entry record number.
    char *name;                     ///< Entry name.
    struct _ntfs_dir_entry *next;   ///< Next entry in the directory.
} ntfs_dir_entry;

/// NTFS directory state.
typedef struct _ntfs_dir_state {
    ntfs_vd *vd;                ///< Directory volume descriptor.
    ntfs_inode *ni;             ///< Directory node descriptor.
    s64 pos;                    ///< Current position in the directory.
    ntfs_dir_entry *first;      ///< The first entry in the directory.
    ntfs_dir_entry *current;    ///< The current entry in the directory.
} ntfs_dir_state;

/* Function prototypes. */

static int       ntfsdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode);
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
static int       ntfsdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2]);
static long      ntfsdev_fpathconf(struct _reent *r, void *fd, int name);
static long      ntfsdev_pathconf(struct _reent *r, const char *path, int name);
static int       ntfsdev_symlink(struct _reent *r, const char *target, const char *linkpath);
static ssize_t   ntfsdev_readlink(struct _reent *r, const char *path, char *buf, size_t bufsiz);

static bool ntfsdev_get_fixed_path(struct _reent *r, const char *path, const char *cwd, ntfs_path *out_path);

static void ntfsdev_fill_stat(ntfs_vd *vd, ntfs_inode *ni, struct stat *st);

static int ntfsdev_dirnext_filldir(void *dirent, const ntfschar *name, const int name_len, const int name_type, const s64 pos, const MFT_REF mref, const unsigned dt_type);

/* Global variables. */

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
    .chmod_r      = ntfsdev_chmod,          ///< Not implemented.
    .fchmod_r     = ntfsdev_fchmod,         ///< Not implemented.
    .rmdir_r      = ntfsdev_unlink,         ///< Exactly the same as unlink.
    .lstat_r      = ntfsdev_stat,
    .utimes_r     = ntfsdev_utimes,
    .fpathconf_r  = ntfsdev_fpathconf,      ///< Not implemented.
    .pathconf_r   = ntfsdev_pathconf,       ///< Not implemented.
    .symlink_r    = ntfsdev_symlink,        ///< Not implemented.
    .readlink_r   = ntfsdev_readlink        ///< Not implemented.
};

static const char g_illegalNtfsChars[] = "\\:*?\"<>|";
static const size_t g_illegalNtfsCharsLength = (MAX_ELEMENTS(g_illegalNtfsChars) - 1);

const devoptab_t *ntfsdev_get_devoptab()
{
    return &ntfsdev_devoptab;
}

static int ntfsdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode)
{
    NX_IGNORE_ARG(mode);

    ntfs_path split_path = {0};

    NTFSDEV_INIT_FILE_VARS;
    NTFSDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!ntfsdev_get_fixed_path(r, path, lun_fs_ctx->cwd, &split_path)) DEVOPTAB_EXIT;

    /* Setup file state. */
    memset(file, 0, sizeof(ntfs_file_state));

    file->vd = fs_ctx;
    file->flags = flags;

    /* Check access mode. */
    switch(flags & O_ACCMODE)
    {
        case O_RDONLY:  /* Read-only. Don't allow append flag. */
            if (flags & O_APPEND) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);
            file->read = true;
            file->write = false;
            file->append = false;
            break;
        case O_WRONLY:  /* Write-only. */
            file->read = false;
            file->write = true;
            file->append = (flags & O_APPEND);
            break;
        case O_RDWR:    /* Read and write. */
            file->read = true;
            file->write = true;
            file->append = (flags & O_APPEND);
            break;
        default:        /* Invalid option. */
            DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);
    }

    USBHSFS_LOG_MSG("Opening file \"%s\" with flags 0x%X (volume \"%s:\").", split_path.path, flags, lun_fs_ctx->name);

    /* Open file. */
    file->ni = ntfs_inode_open_from_path(file->vd, split_path.path);
    if (file->ni)
    {
        /* File already exists. */
        /* Create + exclusive when the file already exists should throw "file exists" error. */
        if ((flags & O_CREAT) && (flags & O_EXCL)) DEVOPTAB_SET_ERROR_AND_EXIT(EEXIST);

        /* Make sure this isn't actually a directory. */
        if (file->ni->mrec->flags & MFT_RECORD_IS_DIRECTORY) DEVOPTAB_SET_ERROR_AND_EXIT(EISDIR);
    } else {
        /* File doesn't exist yet. */
        /* Were we supposed to create this file? */
        if (flags & O_CREAT)
        {
            /* Create the file. */
            USBHSFS_LOG_MSG("Creating \"%s\" (volume \"%s:\").", split_path.path, lun_fs_ctx->name);
            file->ni = ntfs_inode_create(file->vd, &split_path, S_IFREG, NULL);
            if (!file->ni) DEVOPTAB_SET_ERROR_AND_EXIT(errno);
        } else {
            /* Can't open file since it doesn't exist. */
            DEVOPTAB_SET_ERROR_AND_EXIT(ENOENT);
        }
    }

    /* Open file data attribute. */
    file->data = ntfs_attr_open(file->ni, AT_DATA, AT_UNNAMED, 0);
    if (!file->data) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

    /* Determine if this file is compressed and/or encrypted. */
    file->compressed = (NAttrCompressed(file->data) || (file->ni->flags & FILE_ATTR_COMPRESSED));
    file->encrypted = (NAttrEncrypted(file->data) || (file->ni->flags & FILE_ATTR_ENCRYPTED));

    /* We cannot read/write encrypted files. */
    if (file->encrypted) DEVOPTAB_SET_ERROR_AND_EXIT(EACCES);

    /* Check if we're trying to write to a read-only file. */
    if ((file->ni->flags & FILE_ATTR_READONLY) && file->write && !fs_ctx->ignore_read_only_attr) DEVOPTAB_SET_ERROR_AND_EXIT(EROFS);

    /* Truncate the file if requested. */
    if ((flags & O_TRUNC) && file->write)
    {
        USBHSFS_LOG_MSG("Truncating \"%s\" (volume \"%s:\").", split_path.path, lun_fs_ctx->name);

        if (!ntfs_attr_truncate(file->data, 0))
        {
            /* Mark the file as dirty. */
            NInoSetDirty(file->ni);

            /* Mark the file for archiving. */
            file->ni->flags |= FILE_ATTR_ARCHIVE;

            /* Update file last access and modify times. */
            ntfs_inode_update_times_filtered(file->vd, file->ni, NTFS_UPDATE_AMCTIME);
        } else {
            DEVOPTAB_SET_ERROR_AND_EXIT(errno);
        }
    }

    /* Set file current position and length. */
    file->pos = 0;
    file->len = file->data->data_size;

    /* Update last access time. */
    ntfs_inode_update_times_filtered(file->vd, file->ni, NTFS_UPDATE_ATIME);

end:
    /* Clean up if something went wrong. */
    if (DEVOPTAB_IS_ERROR_SET && file)
    {
        if (file->data) ntfs_attr_close(file->data);
        if (file->ni) ntfs_inode_close(file->ni);
        memset(file, 0, sizeof(ntfs_file_state));
    }

    ntfs_path_destroy(&split_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_close(struct _reent *r, void *fd)
{
    NTFSDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!file->ni || !file->data) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Closing file %lu (volume \"%s:\").", file->ni->mft_no, lun_fs_ctx->name);

    /* If the file is dirty, synchronize its data. */
    if (NInoDirty(file->ni)) ntfs_inode_sync(file->ni);

    /* Special case clean-ups for compressed and/or encrypted files. */
    if (file->compressed) ntfs_attr_pclose(file->data);

#ifdef HAVE_SETXATTR
    if (file->encrypted) ntfs_efs_fixup_attribute(NULL, file->data);
#endif

    /* Close file data attribute. */
    ntfs_attr_close(file->data);

    /* Close file node. */
    ntfs_inode_close(file->ni);

    /* Reset file state. */
    memset(file, 0, sizeof(ntfs_file_state));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static ssize_t ntfsdev_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    size_t wr_sz = 0;

    NTFSDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!file->ni || !file->data || !ptr || !len) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Check if the file was opened with write access. */
    if (!file->write) DEVOPTAB_SET_ERROR_AND_EXIT(EBADF);

    /* Check if the append flag is enabled. */
    if (file->append) file->pos = file->len;

    /* Write file data until the requested length is satified. */
    /* This is done like this because writing to compressed files may return partial write sizes instead of the full size in a single call. */
    while(len > 0)
    {
        USBHSFS_LOG_MSG("Writing 0x%lX byte(s) to file %lu at offset 0x%lX (volume \"%s:\").", len, file->ni->mft_no, file->pos, lun_fs_ctx->name);

        s64 written = ntfs_attr_pwrite(file->data, (s64)file->pos, (s64)len, ptr);
        if (written <= 0 || written > (s64)len) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

        wr_sz += written;
        file->pos += written;
        len -= written;
        ptr += written;
    }

end:
    /* Did we write anything? */
    if (file && wr_sz > 0)
    {
        /* Mark the file as dirty. */
        NInoSetDirty(file->ni);

        /* Mark the file for archiving. */
        file->ni->flags |= FILE_ATTR_ARCHIVE;

        /* Update file last access and modify times. */
        ntfs_inode_update_times_filtered(file->vd, file->ni, NTFS_UPDATE_AMCTIME);

        /* Update the files data length. */
        file->len = file->data->data_size;
    }

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT((ssize_t)wr_sz);
}

static ssize_t ntfsdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    size_t rd_sz = 0;

    NTFSDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!file->ni || !file->data || !ptr || !len) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Check if the file was opened with read access. */
    if (!file->read) DEVOPTAB_SET_ERROR_AND_EXIT(EBADF);

    /* Don't read past EOF. */
    if (file->pos + len > file->len)
    {
        r->_errno = EOVERFLOW; // newlib will force an error condition if we set errno to any value other than 0.
        len = (file->len - file->pos);
    }

    /* Read file data until the requested length is satified. */
    /* This is done like this because reading from compressed files may return partial read sizes instead of the full size in a single call. */
    while(len > 0)
    {
        USBHSFS_LOG_MSG("Reading 0x%lX byte(s) from file %lu at offset 0x%lX (volume \"%s:\").", len, file->ni->mft_no, file->pos, lun_fs_ctx->name);

        s64 read = ntfs_attr_pread(file->data, (s64)file->pos, (s64)len, ptr);
        if (read <= 0 || read > (s64)len) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

        rd_sz += read;
        file->pos += read;
        len -= read;
        ptr += read;
    }

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT((ssize_t)rd_sz);
}

static off_t ntfsdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    off_t offset = 0;

    NTFSDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!file->ni || !file->data) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Find the offset to seek from. */
    switch(dir)
    {
        case SEEK_SET:  /* Set absolute position relative to zero (start offset). */
            file->pos = MIN(MAX(pos, 0), file->len);
            break;
        case SEEK_CUR:  /* Set position relative to the current position. */
            file->pos = MIN(MAX(file->pos + pos, 0), file->len);
            break;
        case SEEK_END:  /* Set position relative to EOF. */
            file->pos = MIN(MAX(file->len + pos, 0), file->len);
            break;
        default:        /* Invalid option. */
            DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);
    }

    USBHSFS_LOG_MSG("Seeking to offset 0x%lX from file in %lu (volume \"%s:\").", file->pos, file->ni->mft_no, lun_fs_ctx->name);
    offset = file->pos;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(offset);
}

static int ntfsdev_fstat(struct _reent *r, void *fd, struct stat *st)
{
    NTFSDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!file->ni || !file->data || !st) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Getting file stats for %lu (volume \"%s:\").", file->ni->mft_no, lun_fs_ctx->name);

    /* Get file stats. */
    ntfsdev_fill_stat(file->vd, file->ni, st);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_stat(struct _reent *r, const char *file, struct stat *st)
{
    ntfs_path split_path = {0};
    ntfs_inode *ni = NULL;

    DEVOPTAB_INIT_VARS;
    NTFSDEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!st) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Get fixed path. */
    if (!ntfsdev_get_fixed_path(r, file, lun_fs_ctx->cwd, &split_path)) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Getting stats for \"%s\" (volume \"%s:\").", split_path.path, lun_fs_ctx->name);

    /* Get entry. */
    ni = ntfs_inode_open_from_path(fs_ctx, split_path.path);
    if (!ni) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

    /* Get entry stats. */
    ntfsdev_fill_stat(fs_ctx, ni, st);

end:
    if (ni) ntfs_inode_close(ni);

    ntfs_path_destroy(&split_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_link(struct _reent *r, const char *existing, const char *newLink)
{
    ntfs_path existing_path = {0}, new_path = {0};
    ntfs_inode *ni = NULL;

    DEVOPTAB_INIT_VARS;
    NTFSDEV_INIT_FS_ACCESS;

    /* Get fixed paths. */
    if (!ntfsdev_get_fixed_path(r, existing, lun_fs_ctx->cwd, &existing_path) || !ntfsdev_get_fixed_path(r, newLink, lun_fs_ctx->cwd, &new_path)) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Linking \"%s\" to \"%s\" (volume \"%s:\").", existing_path.path, new_path.path, lun_fs_ctx->name);

    /* Create a symbolic link entry. */
    ni = ntfs_inode_create(fs_ctx, &existing_path, S_IFLNK, new_path.path);
    if (!ni) DEVOPTAB_SET_ERROR(errno);

end:
    if (ni) ntfs_inode_close(ni);

    ntfs_path_destroy(&new_path);

    ntfs_path_destroy(&existing_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_unlink(struct _reent *r, const char *name)
{
    ntfs_path split_path = {0};

    DEVOPTAB_INIT_VARS;
    NTFSDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!ntfsdev_get_fixed_path(r, name, lun_fs_ctx->cwd, &split_path)) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Deleting \"%s\" (volume \"%s:\").", split_path.path, lun_fs_ctx->name);

    /* Unlink entry. */
    if (ntfs_inode_unlink(fs_ctx, &split_path)) DEVOPTAB_SET_ERROR(errno);

end:
    ntfs_path_destroy(&split_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_chdir(struct _reent *r, const char *name)
{
    ntfs_path split_path = {0};
    ntfs_inode *ni = NULL;
    size_t cwd_len = 0;

    DEVOPTAB_INIT_VARS;
    NTFSDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!ntfsdev_get_fixed_path(r, name, lun_fs_ctx->cwd, &split_path)) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Changing current directory to \"%s\" (volume \"%s:\").", split_path.path, lun_fs_ctx->name);

    /* Find directory entry. */
    ni = ntfs_inode_open_from_path(fs_ctx, split_path.path);
    if (!ni) DEVOPTAB_SET_ERROR_AND_EXIT(ENOENT);

    /* Make sure this is indeed a directory. */
    if (!(ni->mrec->flags & MFT_RECORD_IS_DIRECTORY)) DEVOPTAB_SET_ERROR_AND_EXIT(ENOTDIR);

    /* Update current working directory. */
    snprintf(lun_fs_ctx->cwd, LIBUSBHSFS_MAX_PATH, "%s", split_path.path);

    cwd_len = strlen(lun_fs_ctx->cwd);
    if (lun_fs_ctx->cwd[cwd_len - 1] != PATH_SEP)
    {
        lun_fs_ctx->cwd[cwd_len] = PATH_SEP;
        lun_fs_ctx->cwd[cwd_len + 1] = '\0';
    }

    /* Set default devoptab device. */
    usbHsFsMountSetDefaultDevoptabDevice(lun_fs_ctx);

end:
    if (ni) ntfs_inode_close(ni);

    ntfs_path_destroy(&split_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_rename(struct _reent *r, const char *oldName, const char *newName)
{
    ntfs_path old_path = {0}, new_path = {0};
    ntfs_inode *ni = NULL;

    DEVOPTAB_INIT_VARS;
    NTFSDEV_INIT_FS_ACCESS;

    /* Get fixed paths. */
    if (!ntfsdev_get_fixed_path(r, oldName, lun_fs_ctx->cwd, &old_path) || !ntfsdev_get_fixed_path(r, newName, lun_fs_ctx->cwd, &new_path)) DEVOPTAB_EXIT;

    /* Check if there's an entry with the new name. */
    ni = ntfs_inode_open_from_path(fs_ctx, new_path.path);
    if (ni)
    {
        ntfs_inode_close(ni);
        DEVOPTAB_SET_ERROR_AND_EXIT(EEXIST);
    }

    USBHSFS_LOG_MSG("Renaming \"%s\" to \"%s\" (volume \"%s:\").", old_path.path, new_path.path, lun_fs_ctx->name);

    /* Link the old entry with the new one. */
    if (ntfs_inode_link(fs_ctx, &old_path, &new_path)) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

    /* Unlink the old entry. */
    if (ntfs_inode_unlink(fs_ctx, &old_path)) DEVOPTAB_SET_ERROR(errno);

end:
    ntfs_path_destroy(&new_path);

    ntfs_path_destroy(&old_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_mkdir(struct _reent *r, const char *path, int mode)
{
    NX_IGNORE_ARG(mode);

    ntfs_path split_path = {0};
    ntfs_inode *ni = NULL;

    DEVOPTAB_INIT_VARS;
    NTFSDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!ntfsdev_get_fixed_path(r, path, lun_fs_ctx->cwd, &split_path)) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Creating directory \"%s\" (volume \"%s:\").", split_path.path, lun_fs_ctx->name);

    /* Create directory. */
    ni = ntfs_inode_create(fs_ctx, &split_path, S_IFDIR, NULL);
    if (!ni) DEVOPTAB_SET_ERROR(errno);

end:
    if (ni) ntfs_inode_close(ni);

    ntfs_path_destroy(&split_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static DIR_ITER *ntfsdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    ntfs_path split_path = {0};
    DIR_ITER *ret = NULL;

    NTFSDEV_INIT_DIR_VARS;
    NTFSDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!ntfsdev_get_fixed_path(r, path, lun_fs_ctx->cwd, &split_path)) DEVOPTAB_EXIT;

    USBHSFS_LOG_MSG("Opening directory \"%s\" (volume \"%s:\").", split_path.path, lun_fs_ctx->name);

    /* Reset directory state. */
    memset(dir, 0, sizeof(ntfs_dir_state));

    /* Open directory. */
    dir->vd = fs_ctx;
    dir->ni = ntfs_inode_open_from_path(dir->vd, split_path.path);
    if (!dir->ni) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

    /* Make sure this is indeed a directory. */
    if (!(dir->ni->mrec->flags & MFT_RECORD_IS_DIRECTORY)) DEVOPTAB_SET_ERROR_AND_EXIT(ENOTDIR);

    /* Update directory last access time. */
    ntfs_inode_update_times_filtered(dir->vd, dir->ni, NTFS_UPDATE_ATIME);

    /* Update return value. */
    ret = dirState;

end:
    /* Clean up if something went wrong. */
    if (DEVOPTAB_IS_ERROR_SET && dir)
    {
        if (dir->ni) ntfs_inode_close(dir->ni);
        memset(dir, 0, sizeof(ntfs_dir_state));
    }

    ntfs_path_destroy(&split_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_PTR(ret);
}

static int ntfsdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    NTFSDEV_INIT_DIR_VARS;

    /* Sanity check. */
    if (!dir->vd || !dir->ni) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Resetting directory state for %lu (volume \"%s:\").", dir->ni->mft_no, lun_fs_ctx->name);

    /* Reset directory position. */
    dir->pos = 0;

    /* Free directory entries. */
    while(dir->first)
    {
        ntfs_dir_entry *next = dir->first->next;
        if (dir->first->name) free(dir->first->name);
        free(dir->first);
        dir->first = next;
    }

    dir->first = dir->current = NULL;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    ntfs_inode *ni = NULL;

    NTFSDEV_INIT_DIR_VARS;

    /* Sanity check. */
    if (!filename || !filestat || !dir->vd || !dir->ni) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    if (!dir->pos && !dir->first)
    {
        USBHSFS_LOG_MSG("Directory %lu (volume \"%s:\") hasn't been read. Caching all entries first.", dir->ni->mft_no, lun_fs_ctx->name);

        /* Read directory contents. */
        if (ntfs_readdir(dir->ni, &(dir->pos), dirState, ntfsdev_dirnext_filldir)) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

        /* Move to the first entry in the directory. */
        dir->current = dir->first;

        /* Update directory last access time. */
        ntfs_inode_update_times_filtered(dir->vd, dir->ni, NTFS_UPDATE_ATIME);
    }

    /* Check if there's an entry waiting to be fetched (end of directory). */
    if (!dir->current || !dir->current->name) DEVOPTAB_SET_ERROR_AND_EXIT(ENOENT);

    USBHSFS_LOG_MSG("Getting info from next directory %lu entry.", dir->ni->mft_no);

    if (!strcmp(dir->current->name, "."))
    {
        /* We're dealing with the current directory dot entry. */
        ni = dir->ni;
    } else
    if (strcmp(dir->current->name, "..") != 0)
    {
        /* We're **not** dealing with the parent directory dot entry. */
        /* Retrieve inode for the fetched entry. */
        ni = ntfs_pathname_to_inode(dir->vd->vol, dir->ni, dir->current->name);
        if (!ni)
        {
            USBHSFS_LOG_MSG("Failed to retrieve NTFS inode for \"%s\".", dir->current->name);
            DEVOPTAB_SET_ERROR_AND_EXIT(errno);
        }
    }

    /* Copy entry name. */
    sprintf(filename, "%s", dir->current->name);

    if (ni)
    {
        /* Get entry stats. */
        ntfsdev_fill_stat(dir->vd, ni, filestat);

        /* Close inode (if needed). */
        if (ni != dir->ni) ntfs_inode_close(ni);
    } else {
        /* Populate parent directory stats with the minimum necessary data. */
        memset(filestat, 0, sizeof(struct stat));
        filestat->st_mode = (S_IFDIR | (0777 & ~dir->vd->dmask));
        filestat->st_nlink = 1;
    }

    /* Move to the next entry in the directory. */
    dir->current = dir->current->next;

    /* Update directory position. */
    dir->pos++;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    NTFSDEV_INIT_DIR_VARS;

    USBHSFS_LOG_MSG("Closing directory %lu (volume \"%s:\").", dir->ni->mft_no, lun_fs_ctx->name);

    /* Free directory entries. */
    while(dir->first)
    {
        ntfs_dir_entry *next = dir->first->next;
        if (dir->first->name) free(dir->first->name);
        free(dir->first);
        dir->first = next;
    }

    /* Close directory node. */
    if (dir->ni) ntfs_inode_close(dir->ni);

    /* Reset directory state. */
    memset(dir, 0, sizeof(ntfs_dir_state));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{
    NX_IGNORE_ARG(path);

    s64 size = 0;
    s8 delta_bits = 0;

    DEVOPTAB_INIT_VARS;
    NTFSDEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!buf) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Getting filesystem stats for \"%s\" (volume \"%s:\").", path, lun_fs_ctx->name);

    /* Check available free space, if not already known. */
    if (!NVolFreeSpaceKnown(fs_ctx->vol) && ntfs_volume_get_free_space(fs_ctx->vol) < 0) DEVOPTAB_SET_ERROR_AND_EXIT(ENOSPC);

    /* Determine free cluster count. */
    size = MAX(fs_ctx->vol->free_clusters, 0);

    /* Determine free inodes within the free space. */
    delta_bits = (s8)(fs_ctx->vol->cluster_size_bits - fs_ctx->vol->mft_record_size_bits);

    /* Fill filesystem stats. */
    memset(buf, 0, sizeof(struct statvfs));

    buf->f_bsize = fs_ctx->vol->cluster_size;                                                                                               /* Filesystem sector size. */
    buf->f_frsize = fs_ctx->vol->cluster_size;                                                                                              /* Fundamental filesystem sector size. */
    buf->f_blocks = fs_ctx->vol->nr_clusters;                                                                                               /* Total number of sectors in filesystem (in f_frsize units). */
    buf->f_bfree = size;                                                                                                                    /* Free sectors. */
    buf->f_bavail = buf->f_bfree;                                                                                                           /* Available sectors. */
    buf->f_files = ((fs_ctx->vol->mftbmp_na->allocated_size << 3) + (delta_bits >= 0 ? (size <<= delta_bits) : (size >>= -delta_bits)));    /* Total number of inodes in file system. */
    buf->f_ffree = MAX(size + fs_ctx->vol->free_mft_records, 0);                                                                            /* Free inodes. */
    buf->f_favail = buf->f_ffree;                                                                                                           /* Available inodes. */
    buf->f_fsid = lun_fs_ctx->device_id;                                                                                                    /* Filesystem ID. */
    buf->f_flag = (NVolReadOnly(fs_ctx->vol) ? ST_RDONLY : 0);                                                                              /* Filesystem flags. */
    buf->f_namemax = NTFS_MAX_NAME_LEN_BYTES;                                                                                               /* Max filename length (in bytes). */

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_ftruncate(struct _reent *r, void *fd, off_t len)
{
    NTFSDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!file->ni || !file->data || len < 0) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Check if the file was opened with write access. */
    if (!file->write) DEVOPTAB_SET_ERROR_AND_EXIT(EBADF);

    /* For compressed files, only deleting and expanding contents are implemented. */
    if (file->compressed && len > 0 && len < file->data->initialized_size) DEVOPTAB_SET_ERROR_AND_EXIT(EOPNOTSUPP);

    USBHSFS_LOG_MSG("Truncating file in %lu to 0x%lX bytes.", file->ni->mft_no, len);

    if (len > file->data->initialized_size)
    {
        /* Expand file data attribute. */
        char zero = 0;
        if (ntfs_attr_pwrite(file->data, len - 1, 1, &zero) <= 0) DEVOPTAB_SET_ERROR(errno);
    } else {
        /* Truncate file data attribute. */
        if (ntfs_attr_truncate(file->data, len)) DEVOPTAB_SET_ERROR(errno);
    }

end:
    /* Did the file size actually change? */
    if (!DEVOPTAB_IS_ERROR_SET && file && file->len != (u64)file->data->data_size)
    {
        /* Mark the file as dirty. */
        NInoSetDirty(file->ni);

        /* Mark the file for archiving. */
        file->ni->flags |= FILE_ATTR_ARCHIVE;

        /* Update file last access and modify times. */
        ntfs_inode_update_times_filtered(file->vd, file->ni, NTFS_UPDATE_AMCTIME);

        /* Update file data length. */
        file->len = file->data->data_size;
    }

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_fsync(struct _reent *r, void *fd)
{
    NTFSDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!file->ni || !file->data) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Synchronizing data for file in %lu (volume \"%s:\").", file->ni->mft_no, lun_fs_ctx->name);

    /* Synchronize file. */
    if (ntfs_inode_sync(file->ni)) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

    /* Clear dirty status from file. */
    NInoClearDirty(file->ni);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_chmod(struct _reent *r, const char *path, mode_t mode)
{
    ntfs_path split_path = {0};
    ntfs_inode *ni = NULL;
    bool is_dir = false;
    u32 settable = FILE_ATTR_VALID_SET_FLAGS;

    DEVOPTAB_INIT_VARS;
    NTFSDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!ntfsdev_get_fixed_path(r, path, lun_fs_ctx->cwd, &split_path)) DEVOPTAB_EXIT;

    /* Get entry. */
    ni = ntfs_inode_open_from_path(fs_ctx, split_path.path);
    if (!ni) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

    /* Make sure we're using a valid attribute mask. */
    is_dir = (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY);
    if (is_dir) settable |= FILE_ATTR_COMPRESSED;
    if (mode & ~settable) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Change element attributes. */
    if (ntfs_set_ntfs_attrib(ni, (const char*)&mode, sizeof(mode), 0)) DEVOPTAB_SET_ERROR(errno);

end:
    if (ni) ntfs_inode_close(ni);

    ntfs_path_destroy(&split_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_fchmod(struct _reent *r, void *fd, mode_t mode)
{
    NTFSDEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!file->ni || (mode & ~FILE_ATTR_VALID_SET_FLAGS)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Change file attributes. */
    if (ntfs_set_ntfs_attrib(file->ni, (const char*)&mode, sizeof(mode), 0)) DEVOPTAB_SET_ERROR(errno);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int ntfsdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2])
{
    ntfs_path split_path = {0};
    ntfs_inode *ni = NULL;
    struct timespec ts_times[2] = {0};
    ntfs_time ntfs_times[3] = {0};

    DEVOPTAB_INIT_VARS;
    NTFSDEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!ntfsdev_get_fixed_path(r, filename, lun_fs_ctx->cwd, &split_path)) DEVOPTAB_EXIT;

    /* Get entry. */
    ni = ntfs_inode_open_from_path(fs_ctx, split_path.path);
    if (!ni) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

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

    /* Convert POSIX timespec values to Microsoft's NTFS time values. */
    /* utimes() expects an array with this order: last access, last modification. */
    /* ntfs_inode_set_times() expects an array with this order: creation, last modification, last access. */
    /* We will preserve the creation time. */
    ntfs_times[0] = ni->creation_time;
    ntfs_times[1] = timespec2ntfs(ts_times[1]);
    ntfs_times[2] = timespec2ntfs(ts_times[0]);

    USBHSFS_LOG_MSG("Setting last access and modification timestamps for \"%s\" (volume \"%s:\") to %lu and %lu, respectively.", filename, lun_fs_ctx->name, ntfs_times[2], ntfs_times[1]);

    /* Change timestamps. */
    if (ntfs_inode_set_times(ni, (const char*)ntfs_times, sizeof(ntfs_times), 0)) DEVOPTAB_SET_ERROR(errno);

end:
    if (ni) ntfs_inode_close(ni);

    ntfs_path_destroy(&split_path);

    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static long ntfsdev_fpathconf(struct _reent *r, void *fd, int name)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(fd);
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static long ntfsdev_pathconf(struct _reent *r, const char *path, int name)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static int ntfsdev_symlink(struct _reent *r, const char *target, const char *linkpath)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(target);
    NX_IGNORE_ARG(linkpath);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static ssize_t ntfsdev_readlink(struct _reent *r, const char *path, char *buf, size_t bufsiz)
{
    NX_IGNORE_ARG(r);
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(buf);
    NX_IGNORE_ARG(bufsiz);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

static bool ntfsdev_get_fixed_path(struct _reent *r, const char *path, const char *cwd, ntfs_path *out_path)
{
    DEVOPTAB_INIT_ERROR_STATE;

    const u8 *p = (const u8*)path;
    ssize_t units = 0;
    u32 code = 0;

    char *out = __usbhsfs_dev_path_buf;
    const size_t out_sz = MAX_ELEMENTS(__usbhsfs_dev_path_buf);
    size_t out_pos = 0;

    char *path_elem = NULL;
    int depth = 0;

    bool finished = false;

    if (!r || !path || !*path || !cwd || !out_path) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    USBHSFS_LOG_MSG("Input path: \"%s\".", path);

    /* Move the path pointer to the start of the actual path. */
    do {
        units = decode_utf8(&code, p);
        if (units < 0) DEVOPTAB_SET_ERROR_AND_EXIT(EILSEQ);
        p += units;
    } while(code >= ' ' && code != ':');

    /* We found a colon; p points to the actual path. */
    if (code == ':') path = (const char*)p;

    /* NTFS-3G doesn't seem to support dot entries in input paths, so we'll need to resolve relative paths on our own. */
    /* We'll also replace consecutive path separators with a single one, make sure there are no illegal NTFS characters, and check if the remainder of the string is valid UTF-8. */
    if (*path == PATH_SEP)
    {
        /* We're dealing with an absolute path. Let's build the output path from scratch using what we have. */
        out[0] = PATH_SEP;
        out[1] = '\0';
        p = (const u8*)(path + 1);
    } else {
        /* We're dealing with a relative path. Let's build the output path using the current working directory as our start point. */
        snprintf(out, out_sz, "%s", cwd);
        p = (const u8*)path;

        /* Determine our current directory depth. */
        const size_t len = strlen(cwd);
        for(size_t i = 1; i < len; i++)
        {
            if (cwd[i] == PATH_SEP) depth++;
        }
    }

    /* Process the remainder of our input path. */
    out_pos = strlen(out);
    path_elem = (out + out_pos);

    while(true)
    {
        /* Copy current path element. */
        do {
            units = decode_utf8(&code, p);

            /* Don't tolerate invalid UTF-8 nor illegal NTFS characters. */
            if (units < 0) DEVOPTAB_SET_ERROR_AND_EXIT(EILSEQ);
            if (memchr(g_illegalNtfsChars, (int)code, g_illegalNtfsCharsLength)) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

            /* Copy character(s) if we haven't reached the end of this path element. */
            if (code >= ' ' && code != PATH_SEP)
            {
                /* Sanity check. */
                if ((out_pos + (size_t)units) >= out_sz) DEVOPTAB_SET_ERROR_AND_EXIT(ENAMETOOLONG);

                memcpy(out + out_pos, p, (size_t)units);

                out_pos += (size_t)units;
                p += units;
            }
        } while(code >= ' ' && code != PATH_SEP);

        /* Set NULL terminator for our current path element. */
        out[out_pos] = '\0';

        if (code < ' ')
        {
            /* Reached the end of the input path. */
            finished = true;
        } else
        if (code == PATH_SEP)
        {
            /* Found a path separator. Let's skip consecutive path separators if they exist. */
            while(*p == PATH_SEP) p++;
        }

        if (!strcmp(path_elem, "."))
        {
            /* We're dealing with a current directory dot entry alias. */
            /* We'll simply remove it from the fixed path. */
            out[--out_pos] = '\0';
        } else
        if (!strcmp(path_elem, ".."))
        {
            /* We're dealing with a parent directory dot entry alias. */
            /* First check if we aren't currently at the root directory. */
            if (depth <= 0) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

            /* Remove dot entry and its preceding path separator from the output string. */
            out[--out_pos] = '\0'; // '.'
            out[--out_pos] = '\0'; // '.'
            out[--out_pos] = '\0'; // '/'

            /* Find previous ocurrence of a path separator in the output string. */
            char* path_sep = (out + out_pos - 1);
            while(path_sep != out && *path_sep != PATH_SEP) path_sep--;

            /* Sanity check. */
            if (path_sep == out) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

            /* Update path element pointer and directory depth. */
            *(++path_sep) = '\0';
            path_elem = path_sep;
            depth--;

            /* Make sure our current position points to the right place. */
            out_pos = (size_t)(path_elem - out);
        } else {
            /* Completely new path element. Make sure its length is valid. */
            if (strlen(path_elem) > NTFS_MAX_NAME_LEN_BYTES) DEVOPTAB_SET_ERROR_AND_EXIT(ENAMETOOLONG);

            /* Append path separator. */
            out[out_pos++] = PATH_SEP;
            out[out_pos] = '\0';

            /* Update path element pointer and directory depth. */
            path_elem = (out + out_pos);
            depth++;
        }

        if (finished) break;
    }

    /* Remove trailing path separator. */
    if (out_pos > 1 && out[--out_pos] == PATH_SEP) out[out_pos] = '\0';

    /* Clear output NTFS path. */
    ntfs_path_destroy(out_path);

    /* Setup NTFS path pointer. */
    out_path->path = strdup(out);

    /* Split the path into separate directory and filename parts. */
    /* e.g. "/dir/file.txt" => dir: "/dir", name: "file.txt". */
    char *path_sep = strrchr(out, PATH_SEP);

    /* Remove the path separator we just found. */
    *path_sep = '\0';

    /* If there's only a single path separator at the start of the string, set the directory string to a path separator. */
    /* Otherwise, just use the path string as-is -- it will get cut off at the path separator we just removed. */
    out_path->dir = strdup(path_sep == out ? "/" : out);

    /* Update the entry name pointer. */
    out_path->name = (*(++path_sep) ? strdup(path_sep) : NULL);

    /* Sanity check. */
    if (!out_path->path || !out_path->dir || (*path_sep && !out_path->name)) DEVOPTAB_SET_ERROR_AND_EXIT(ENOMEM);

    USBHSFS_LOG_MSG("Output strings -> Path: \"%s\" | Directory: \"%s\" | Name: \"%s\".", out_path->path, out_path->dir, out_path->name);

end:
    if (DEVOPTAB_IS_ERROR_SET) ntfs_path_destroy(out_path);

    DEVOPTAB_RETURN_BOOL;
}

static void ntfsdev_fill_stat(ntfs_vd *vd, ntfs_inode *ni, struct stat *st)
{
    ntfs_attr *na = NULL;

    /* Clear stat struct. */
    memset(st, 0, sizeof(struct stat));

    /* Fill stat struct. */
    st->st_dev = vd->id;
    st->st_ino = ni->mft_no;
    st->st_uid = vd->uid;
    st->st_gid = vd->gid;

    if (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY)
    {
        /* We're dealing with a directory entry. */
        st->st_mode = (S_IFDIR | (0777 & ~vd->dmask));
        st->st_nlink = 1;

        /* Open the directory index allocation table attribute to get size stats. */
        na = ntfs_attr_open(ni, AT_INDEX_ALLOCATION, NTFS_INDEX_I30, 4);
        if (na)
        {
            st->st_size = na->data_size;
            st->st_blocks = (na->allocated_size >> 9);
            ntfs_attr_close(na);
        }
    } else {
        /* We're dealing with a file entry. */
        st->st_mode = (S_IFREG | (0777 & ~vd->fmask));
        st->st_nlink = le16_to_cpu(ni->mrec->link_count);
        st->st_size = ni->data_size;
        st->st_blocks = ((ni->allocated_size + 511) >> 9);
    }

    /* Convert Microsoft's NTFS time values to POSIX timespec values. */
    st->st_atim = ntfs2timespec(ni->last_access_time);
    st->st_mtim = ntfs2timespec(ni->last_data_change_time);
    st->st_ctim = ntfs2timespec(ni->creation_time);

    /* Store NTFS-specific file flags. */
    st->st_spare4[0] = ni->flags;
}

static int ntfsdev_dirnext_filldir(void *dirent, const ntfschar *name, const int name_len, const int name_type, const s64 pos, const MFT_REF mref, const unsigned dt_type)
{
    NX_IGNORE_ARG(pos);
    NX_IGNORE_ARG(dt_type);

    DIR_ITER *dirState = (DIR_ITER*)dirent;
    ntfs_dir_entry *entry = NULL;
    char *entry_name = NULL;

    DEVOPTAB_INIT_ERROR_STATE;
    DEVOPTAB_DECL_DIR_STATE(ntfs_dir_state);

    /* Ignore DOS file names. */
    if (name_type == FILE_NAME_DOS) DEVOPTAB_EXIT;

    /* Convert the entry name from UTF-16LE into our current locale (UTF-8). */
    if (ntfs_ucstombs(name, name_len, &entry_name, 0) <= 0) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

    /* Check if we're not dealing with parent and current directory entries (dot entries). */
    if (strcmp(entry_name, ".") != 0 && strcmp(entry_name, "..") != 0)
    {
        /* Make sure this entry exists under the directory we're processing. */
        ntfs_inode *ni = ntfs_pathname_to_inode(dir->vd->vol, dir->ni, entry_name);
        if (!ni)
        {
            USBHSFS_LOG_MSG("Failed to retrieve NTFS inode for \"%s\".", entry_name);
            DEVOPTAB_SET_ERROR_AND_EXIT(errno);
        }

        ntfs_inode_close(ni);

        USBHSFS_LOG_MSG("Found entry \"%s\" with MREF %lu.", entry_name, mref);
    }

    /* Allocate a new directory entry. */
    entry = malloc(sizeof(ntfs_dir_entry));
    if (!entry) DEVOPTAB_SET_ERROR_AND_EXIT(errno);

    /* Setup the directory entry. */
    entry->mref = MREF(mref);
    entry->name = entry_name;
    entry->next = NULL;

    /* Link entry to the list of directory entries. */
    if (!dir->first)
    {
        dir->first = entry;
    } else {
        ntfs_dir_entry *last = dir->first;
        while(last->next) last = last->next;
        last->next = entry;
    }

end:
    if (DEVOPTAB_IS_ERROR_SET)
    {
        if (entry_name) free(entry_name);
        if (entry) free(entry);
    }

    DEVOPTAB_RETURN_INT(0);
}
