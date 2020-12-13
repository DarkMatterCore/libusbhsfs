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

#include <sys/param.h>
#include <fcntl.h>

#include "ntfs.h"
#include "ntfs_more.h"

#include "../usbhsfs_manager.h"
#include "../usbhsfs_mount.h"

/* Helper macros. */

#define ntfs_end                    goto end
#define ntfs_ended_with_error       (_errno != 0)
#define ntfs_set_error(x)           r->_errno = _errno = (x)
#define ntfs_set_error_and_exit(x)  \
do { \
    ntfs_set_error((x)); \
    ntfs_end; \
} while(0)

#define ntfs_declare_error_state    int _errno = 0
#define ntfs_declare_file_state     ntfs_file_state *file = (ntfs_file_state*)fd
#define ntfs_declare_dir_state      ntfs_dir_state *dir = (ntfs_dir_state*)dirState->dirStruct
#define ntfs_declare_fs_ctx         UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*)r->deviceData
#define ntfs_declare_drive_ctx      UsbHsFsDriveContext *drive_ctx = usbHsFsManagerGetDriveContextByFileSystemContextAndAcquireLock(&fs_ctx)
#define ntfs_declare_vol_state      ntfs_vd *vd = fs_ctx->ntfs

#define ntfs_lock_drive_ctx         ntfs_declare_fs_ctx; \
                                    ntfs_declare_drive_ctx; \
                                    if (!drive_ctx) ntfs_set_error_and_exit(ENODEV);

#define ntfs_unlock_drive_ctx       if (drive_ctx) mutexUnlock(&(drive_ctx->mutex))

#define ntfs_return(x)              return (ntfs_ended_with_error ? -1 : (x))
#define ntfs_return_ptr(x)          return (ntfs_ended_with_error ? NULL : (x))

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
static int       ntfsdev_rmdir(struct _reent *r, const char *name);
static int       ntfsdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2]);

static int ntfsdev_diropen_filldir(void *dirent, const ntfschar *name, const int name_len, const int name_type, const s64 pos, const MFT_REF mref, const unsigned dt_type);

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
    .rmdir_r      = ntfsdev_rmdir,
    .lstat_r      = ntfsdev_stat,
    .utimes_r     = ntfsdev_utimes
};

const devoptab_t *ntfsdev_get_devoptab()
{
    return &ntfsdev_devoptab;
}

static int ntfsdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode)
{
    (void)mode;
    
    ntfs_declare_error_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;
    ntfs_declare_vol_state;
    
    /* Sanity check. */
    if (!file || !path || !*path) ntfs_set_error_and_exit(EINVAL);
    
    /* Setup file state. */
    memset(file, 0, sizeof(ntfs_file_state));
    file->vd = vd;
    
    /* Check access mode. */
    switch(flags & O_ACCMODE)
    {
        case O_RDONLY:  /* Read-only. Don't allow append flag. */
            if (flags & O_APPEND) ntfs_set_error_and_exit(EINVAL);
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
            ntfs_set_error_and_exit(EINVAL);
    }
    
    USBHSFS_LOG("Opening file \"%s\" with flags 0x%X.", path, flags);
    
    /* Open file. */
    file->ni = ntfs_inode_open_from_path(file->vd, path);
    if (file->ni)
    {
        /* The file already exists, check flags. */
        /* Create + exclusive when the file already exists should throw "file exists" error. */
        if ((flags & O_CREAT) && (flags & O_EXCL)) ntfs_set_error_and_exit(EEXIST);
        
        /* Ensure that this file is not actually a directory */
        if (file->ni->mrec->flags & MFT_RECORD_IS_DIRECTORY) ntfs_set_error_and_exit(EISDIR);
    } else {
        /* The file doesn't exist yet. Check flags. */
        /* Were we suppose to create this file? */
        if (flags & O_CREAT)
        {
            /* Create the file */
            USBHSFS_LOG("Creating \"%s\".", path);
            file->ni = ntfs_inode_create(file->vd, path, S_IFREG, NULL);
            if (!file->ni) ntfs_set_error_and_exit(errno);
        } else {
            /* Can't open file, does not exist. */
            ntfs_set_error_and_exit(ENOENT);
        }
    }
    
    /* Open file data attribute. */
    file->data = ntfs_attr_open(file->ni, AT_DATA, AT_UNNAMED, 0);
    if (!file->data) ntfs_set_error_and_exit(errno);
    
    /* Determine if this file is compressed and/or encrypted. */
    file->compressed = (NAttrCompressed(file->data) || (file->ni->flags & FILE_ATTR_COMPRESSED));
    file->encrypted = (NAttrEncrypted(file->data) || (file->ni->flags & FILE_ATTR_ENCRYPTED));
    
    /* We cannot read/write encrypted files. */
    if (file->encrypted) ntfs_set_error_and_exit(EACCES);
    
    /* Check if we're trying to write to a read-only file. */
    if ((file->ni->flags & FILE_ATTR_READONLY) && file->write && !vd->ignore_read_only_attr) ntfs_set_error_and_exit(EROFS);
    
    /* Truncate the file if requested. */
    if ((flags & O_TRUNC) && file->write)
    {
        USBHSFS_LOG("Truncating \"%s\".", path);
        
        if (!ntfs_attr_truncate(file->data, 0))
        {
            /* Mark the file as dirty. */
            NInoSetDirty(file->ni);
            
            /* Mark the file for archiving. */
            file->ni->flags |= FILE_ATTR_ARCHIVE;
            
            /* Update file last access and modify times. */
            ntfs_inode_update_times_filtered(file->vd, file->ni, NTFS_UPDATE_AMCTIME);
        } else {
            ntfs_set_error_and_exit(errno);
        }
    }
    
    /* Set file current position and length. */
    file->pos = 0;
    file->len = file->data->data_size;
    
    /* Update last access time. */
    ntfs_inode_update_times_filtered(file->vd, file->ni, NTFS_UPDATE_ATIME);
    
end:
    /* Clean up if something went wrong. */
    if (ntfs_ended_with_error && file)
    {
        if (file->data) ntfs_attr_close(file->data);
        
        if (file->ni) ntfs_inode_close(file->ni);
        
        memset(file, 0, sizeof(ntfs_file_state));
    }
    
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_close(struct _reent *r, void *fd)
{
    ntfs_declare_error_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file || !file->ni || !file->data) ntfs_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Closing file %lu.", file->ni->mft_no);
    
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
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static ssize_t ntfsdev_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    size_t wr_sz = 0;
    
    ntfs_declare_error_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file || !file->ni || !file->data || !ptr || !len) ntfs_set_error_and_exit(EINVAL);
    
    /* Check if the file was opened with write access. */
    if (!file->write) ntfs_set_error_and_exit(EBADF);
    
    /* Check if the append flag is enabled. */
    if (file->append) file->pos = file->len;
    
    /* Write file data until the requested length is satified. */
    /* This is done like this because writing to compressed files may return partial write sizes instead of the full size in a single call. */
    while(len > 0)
    {
        USBHSFS_LOG("Writing 0x%lX byte(s) to file %lu at offset 0x%lX.", len, file->ni->mft_no, file->pos);
        
        s64 written = ntfs_attr_pwrite(file->data, (s64)file->pos, (s64)len, ptr);
        if (written <= 0 || written > (s64)len) ntfs_set_error_and_exit(errno);
        
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
    
    ntfs_unlock_drive_ctx;
    ntfs_return((ssize_t)wr_sz);
}

static ssize_t ntfsdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    size_t rd_sz = 0;
    
    ntfs_declare_error_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file || !file->ni || !file->data || !ptr || !len) ntfs_set_error_and_exit(EINVAL);
    
    /* Check if the file was opened with read access. */
    if (!file->read) ntfs_set_error_and_exit(EBADF);
    
    /* Don't read past EOF. */
    if (file->pos + len > file->len)
    {
        r->_errno = EOVERFLOW;
        len = (file->len - file->pos);
    }
    
    /* Read file data until the requested length is satified. */
    /* This is done like this because reading from compressed files may return partial read sizes instead of the full size in a single call. */
    while(len > 0)
    {
        USBHSFS_LOG("Reading 0x%lX byte(s) from file %lu at offset 0x%lX.", len, file->ni->mft_no, file->pos);
        
        s64 read = ntfs_attr_pread(file->data, (s64)file->pos, (s64)len, ptr);
        if (read <= 0 || read > (s64)len) ntfs_set_error_and_exit(errno);
        
        rd_sz += read;
        file->pos += read;
        len -= read;
        ptr += read;
    }
    
end:
    ntfs_unlock_drive_ctx;
    ntfs_return((ssize_t)rd_sz);
}

static off_t ntfsdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    off_t offset = 0;
    
    ntfs_declare_error_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file || !file->ni || !file->data) ntfs_set_error_and_exit(EINVAL);
    
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
            ntfs_set_error_and_exit(EINVAL);
    }
    
    USBHSFS_LOG("Seeking to offset 0x%lX from file in %lu.", file->pos, file->ni->mft_no);
    offset = file->pos;

end:
    ntfs_unlock_drive_ctx;
    ntfs_return(offset);
}

static int ntfsdev_fstat(struct _reent *r, void *fd, struct stat *st)
{
    ntfs_declare_error_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file || !file->ni || !file->data || !st) ntfs_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Getting file stats for %lu.", file->ni->mft_no);
    
    /* Get file stats. */
    if (ntfs_inode_stat(file->vd, file->ni, st)) ntfs_set_error(errno);
    
end:
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_stat(struct _reent *r, const char *path, struct stat *st)
{
    ntfs_inode *ni = NULL;
    
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    ntfs_declare_vol_state;
    
    /* Sanity check. */
    if (!path || !*path || !st) ntfs_set_error_and_exit(EINVAL);
    
    /* Short circuit for current and parent directory references. */
    if (!strcmp(path, NTFS_ENTRY_NAME_SELF) || !strcmp(path, NTFS_ENTRY_NAME_PARENT))
    {
        memset(st, 0, sizeof(struct stat));
        st->st_mode = S_IFDIR;
        ntfs_end;
    }
    
    USBHSFS_LOG("Getting stats for \"%s\".", path);
    
    /* Get entry. */
    ni = ntfs_inode_open_from_path(vd, path);
    if (!ni) ntfs_set_error_and_exit(errno);
    
    /* Get entry stats. */
    if (ntfs_inode_stat(vd, ni, st)) ntfs_set_error(errno);
    
end:
    if (ni) ntfs_inode_close(ni);
    
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_link(struct _reent *r, const char *existing, const char *newLink)
{
    ntfs_inode *ni = NULL;
    
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    ntfs_declare_vol_state;
    
    /* Sanity check. */
    if (!existing || !*existing || !newLink || !*newLink) ntfs_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Linking \"%s\" -> \"%s\".", existing, newLink);
    
    /* Create a symbolic link entry. */
    ni = ntfs_inode_create(vd, existing, S_IFLNK, newLink);
    if (!ni) ntfs_set_error(errno);
    
end:
    if (ni) ntfs_inode_close(ni);
    
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_unlink(struct _reent *r, const char *name)
{
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    ntfs_declare_vol_state;
    
    /* Sanity check. */
    if (!name || !*name) ntfs_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Deleting \"%s\".", name);
    
    /* Unlink entry. */
    if (ntfs_inode_unlink(vd, name)) ntfs_set_error(errno);
    
end:
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_chdir(struct _reent *r, const char *name)
{
    ntfs_path path = {0};
    ntfs_inode *ni = NULL, *old_cwd = NULL;
    
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    ntfs_declare_vol_state;
    
    /* Safety check. */
    if (!name || !*name) ntfs_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Changing current directory to \"%s\".", name);
    
    /* Resolve directory path. */
    if (ntfs_resolve_path(vd, name, &path)) ntfs_set_error_and_exit(errno);
    
    /* Find directory entry. */
    ni = ntfs_inode_open_from_path(vd, name);
    if (!ni) ntfs_set_error_and_exit(ENOENT);
    
    /* Make sure this is indeed a directory. */
    if (!(ni->mrec->flags & MFT_RECORD_IS_DIRECTORY)) ntfs_set_error_and_exit(ENOTDIR);
    
    /* Swap the current directory references around. */
    old_cwd = vd->cwd;
    vd->cwd = ni;
    ni = old_cwd;
    
    /* Update current directory. */
    sprintf(fs_ctx->cwd, "%s", path.path);
    
    /* Set default devoptab device. */
    usbHsFsMountSetDefaultDevoptabDevice(fs_ctx);
    
end:
    if (ni) ntfs_inode_close(ni);
    
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_rename(struct _reent *r, const char *oldName, const char *newName)
{
    ntfs_inode *ni = NULL;
    
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    ntfs_declare_vol_state;
    
    /* Safety check. */
    if (!oldName || !*oldName || !newName || !*newName) ntfs_set_error_and_exit(EINVAL);
    
    /* TO DO: check if both paths belong to the same device. */
    //if (vd != ntfs_volume_from_pathname(newName)) ntfs_set_error_and_exit(EXDEV);
    
    /* Check if there's an entry with the new name. */
    ni = ntfs_inode_open_from_path(vd, newName);
    if (ni) 
    {
        ntfs_inode_close(ni);
        ntfs_set_error_and_exit(EEXIST);
    }
    
    USBHSFS_LOG("Renaming \"%s\" to \"%s\".", oldName, newName);
    
    /* Link the old entry with the new one. */
    if (ntfs_inode_link(vd, oldName, newName)) ntfs_set_error_and_exit(errno);
    
    /* Unlink the old entry. */
    if (ntfs_inode_unlink(vd, oldName)) ntfs_set_error(errno);
    
end:
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_mkdir(struct _reent *r, const char *path, int mode)
{
    (void)mode;
    
    ntfs_inode *ni = NULL;
    
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    ntfs_declare_vol_state;
    
    /* Safety check. */
    if (!path || !*path) ntfs_set_error_and_exit(EINVAL);

    USBHSFS_LOG("Creating directory \"%s\".", path);
    
    /* Create directory. */
    ni = ntfs_inode_create(vd, path, S_IFDIR, NULL);
    if (!ni) ntfs_set_error(errno);
    
end:
    if (ni) ntfs_inode_close(ni);
    
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static DIR_ITER *ntfsdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    s64 position = 0;
    ntfs_dir_state *dir = NULL;
    DIR_ITER *ret = NULL;
    
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    ntfs_declare_vol_state;
    
    /* Sanity check. */
    if (!dirState || !path || !*path) ntfs_set_error_and_exit(EINVAL);
    
    /* Setup directory descriptor. */
    dir = (ntfs_dir_state*)dirState->dirStruct;
    memset(dir, 0, sizeof(ntfs_dir_state));
    dir->vd = vd;
    
    USBHSFS_LOG("Opening directory \"%s\".", path);
    
    /* Open directory. */
    dir->ni = ntfs_inode_open_from_path(dir->vd, path);
    if (!dir->ni) ntfs_set_error_and_exit(ENOENT);
    
    /* Make sure this is indeed a directory. */
    if (!(dir->ni->mrec->flags & MFT_RECORD_IS_DIRECTORY)) ntfs_set_error_and_exit(ENOTDIR);
    
    /* Read directory contents. */
    dir->first = dir->current = NULL;
    if (ntfs_readdir(dir->ni, &position, dirState, ntfsdev_diropen_filldir)) ntfs_set_error_and_exit(errno);
    
    /* Move to the first entry in the directory. */
    dir->current = dir->first;
    
    /* Update directory last access time. */
    ntfs_inode_update_times_filtered(dir->vd, dir->ni, NTFS_UPDATE_ATIME);
    
    /* Update return value. */
    ret = dirState;
    
end:
    /* Clean up if something went wrong. */
    if (ntfs_ended_with_error && dir)
    {
        if (dir->first)
        {
            while(dir->first)
            {
                ntfs_dir_entry *next = dir->first->next;
                if (dir->first->name) free(dir->first->name);
                free(dir->first);
                dir->first = next;
            }
        }
        
        if (dir->ni) ntfs_inode_close(dir->ni);
        
        memset(dir, 0, sizeof(ntfs_dir_state));
    }
    
    ntfs_unlock_drive_ctx;
    ntfs_return_ptr(ret);
}

static int ntfsdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    
    /* Sanity check. */
    if (!dirState) ntfs_set_error_and_exit(EINVAL);
    
    ntfs_declare_dir_state;
    
    USBHSFS_LOG("Resetting directory state from %lu.", dir->ni->mft_no);
    
    /* Move to the first entry in the directory. */
    dir->current = dir->first;
    
    /* Update directory last access time. */
    ntfs_inode_update_times_filtered(dir->vd, dir->ni, NTFS_UPDATE_ATIME);
    
end:
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    ntfs_inode *ni = NULL;
    
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    
    /* Sanity check. */
    if (!dirState || !filename || !filestat) ntfs_set_error_and_exit(EINVAL);
    
    ntfs_declare_dir_state;
    
    /* Check if there's an entry waiting to be fetched (end of directory). */
    if (!dir->current || !dir->current->name) ntfs_set_error_and_exit(ENOENT);
    
    /* Fetch current entry name. */
    strcpy(filename, dir->current->name);
    
    if (!strcmp(dir->current->name, NTFS_ENTRY_NAME_SELF) || !strcmp(dir->current->name, NTFS_ENTRY_NAME_PARENT))
    {
        /* Current/parent directory alias. */
        memset(filestat, 0, sizeof(struct stat));
        filestat->st_mode = S_IFDIR;
    } else {
        /* Regular entry. */
        ni = ntfs_inode_open_from_path(dir->vd, dir->current->name);
        if (!ni)
        {
            /* Reached end of directory */
            dir->current = NULL;
            ntfs_set_error_and_exit(errno);
        }
        
        ntfs_inode_stat(dir->vd, ni, filestat);
        ntfs_inode_close(ni);
    }
    
    /* Move to the next entry in the directory. */
    dir->current = dir->current->next;
    
    /* Update directory last access time. */
    ntfs_inode_update_times_filtered(dir->vd, dir->ni, NTFS_UPDATE_ATIME);
    
end:
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    
    /* Sanity check. */
    if (!dirState) ntfs_set_error_and_exit(EINVAL);
    
    ntfs_declare_dir_state;
    
    USBHSFS_LOG("Closing directory %lu.", dir->ni->mft_no);
    
    /* Free directory entries (if needed). */
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
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{
    (void)path;
    
    s64 size = 0;
    s8 delta_bits = 0;
    
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    ntfs_declare_vol_state;
    
    /* Sanity check. */
    if (!buf) ntfs_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Getting filesystem stats for \"%s\".", path);
    
    /* Check available free space. */
    if (ntfs_volume_get_free_space(vd->vol) < 0) ntfs_set_error_and_exit(ENOSPC);
    
    /* Determine free cluster count. */
    size = MAX(vd->vol->free_clusters, 0);
    
    /* Determine free inodes within the free space. */
    delta_bits = (s8)(vd->vol->cluster_size_bits - vd->vol->mft_record_size_bits);
    
    /* Fill filesystem stats. */
    memset(buf, 0, sizeof(struct statvfs));
    
    buf->f_bsize = vd->vol->cluster_size;                                                                                               /* Filesystem sector size. */
    buf->f_frsize = vd->vol->cluster_size;                                                                                              /* Fundamental filesystem sector size. */
    buf->f_blocks = vd->vol->nr_clusters;                                                                                               /* Total number of sectors in filesystem (in f_frsize units). */
    buf->f_bfree = size;                                                                                                                /* Free sectors. */
    buf->f_bavail = buf->f_bfree;                                                                                                       /* Available sectors. */
    buf->f_files = ((vd->vol->mftbmp_na->allocated_size << 3) + (delta_bits >= 0 ? (size <<= delta_bits) : (size >>= -delta_bits)));    /* Total number of inodes in file system. */
    buf->f_ffree = MAX(size + vd->vol->free_mft_records, 0);                                                                            /* Free inodes. */
    buf->f_favail = buf->f_ffree;                                                                                                       /* Available inodes. */
    buf->f_fsid = vd->id;                                                                                                               /* Filesystem ID. */
    buf->f_flag = (NVolReadOnly(vd->vol) ? ST_RDONLY : 0);                                                                              /* Filesystem flags. */
    buf->f_namemax = NTFS_MAX_NAME_LEN;                                                                                                 /* Max filename length. */
    
end:
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_ftruncate(struct _reent *r, void *fd, off_t len)
{
    char zero = 0;
    
    ntfs_declare_error_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file || !file->ni || !file->data || len < 0) ntfs_set_error_and_exit(EINVAL);
    
    /* Check if the file was opened with write access. */
    if (!file->write) ntfs_set_error_and_exit(EBADF);

    /* For compressed files, only deleting and expanding contents are implemented. */
    if (file->compressed && len > 0 && len < file->data->initialized_size) ntfs_set_error_and_exit(EOPNOTSUPP);
    
    USBHSFS_LOG("Truncating file in %lu to 0x%lX bytes.", file->ni->mft_no, len);
    
    if (len > file->data->initialized_size)
    {
        /* Expand file data attribute. */
        if (ntfs_attr_pwrite(file->data, len - 1, 1, &zero) <= 0) ntfs_set_error(errno);
    } else {
        /* Truncate file data attribute. */
        if (ntfs_attr_truncate(file->data, len)) ntfs_set_error(errno);
    }
    
end:
    /* Did the file size actually change? */
    if (file && file->len != (u64)file->data->data_size)
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
    
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_fsync(struct _reent *r, void *fd)
{
    ntfs_declare_error_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;
    
    /* Sanity check. */
    if (!file || !file->ni || !file->data) ntfs_set_error_and_exit(EINVAL);
    
    USBHSFS_LOG("Synchronizing data for file in %lu.", file->ni->mft_no);
    
    /* Synchronize file. */
    if (ntfs_inode_sync(file->ni)) ntfs_set_error_and_exit(errno);
    
    /* Clear dirty status from file. */
    NInoClearDirty(file->ni);
    
end:
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_chmod(struct _reent *r, const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    
    /* Not implemented. */
    r->_errno = ENOSYS;
    return -1;
}

static int ntfsdev_fchmod(struct _reent *r, void *fd, mode_t mode)
{
    (void)fd;
    (void)mode;
    
    /* Not implemented. */
    r->_errno = ENOSYS;
    return -1;
}

static int ntfsdev_rmdir(struct _reent *r, const char *name)
{
    /* Exactly the same as ntfsdev_unlink(). */
    return ntfsdev_unlink(r, name);
}

static int ntfsdev_utimes(struct _reent *r, const char *filename, const struct timeval times[2])
{
    ntfs_inode *ni = NULL;
    struct timespec ts_times[2] = {0};
    ntfs_time ntfs_times[3] = {0};
    
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;
    ntfs_declare_vol_state;
    
    /* Sanity check. */
    if (!filename || !*filename) ntfs_set_error_and_exit(EINVAL);
    
    /* Get entry. */
    ni = ntfs_inode_open_from_path(vd, filename);
    if (!ni) ntfs_set_error_and_exit(errno);
    
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
    /* utimes() expects an array with this order: access, update. */
    /* ntfs_inode_set_times() expects an array with this order: create, update, access. */
    /* We will preserve the creation time. */
    ntfs_times[0] = ni->creation_time;
    ntfs_times[1] = timespec2ntfs(ts_times[1]);
    ntfs_times[2] = timespec2ntfs(ts_times[0]);
    
    USBHSFS_LOG("Setting last access and modification times for \"%s\" to 0x%lX and 0x%lX, respectively.", filename, ntfs_times[2], ntfs_times[1]);
    
    /* Change timestamps. */
    if (ntfs_inode_set_times(ni, (const char*)ntfs_times, sizeof(ntfs_times), 0)) ntfs_set_error(errno);
    
end:
    if (ni) ntfs_inode_close(ni);
    
    ntfs_unlock_drive_ctx;
    ntfs_return(0);
}

static int ntfsdev_diropen_filldir(void *dirent, const ntfschar *name, const int name_len, const int name_type, const s64 pos, const MFT_REF mref, const unsigned dt_type)
{
    (void)pos;
    (void)dt_type;
    
    DIR_ITER *dirState = (DIR_ITER*)dirent;
    ntfs_inode *ni = NULL;
    ntfs_dir_entry *entry = NULL;
    char *entry_name = NULL;
    
    ntfs_declare_error_state;
    ntfs_declare_dir_state;
    
    /* Ignore DOS file names. */
    if (name_type == FILE_NAME_DOS) ntfs_end;
    
    /* Check if this entry can be enumerated (as described by the volume descriptor). */
    if (MREF(mref) < FILE_first_user && MREF(mref) != FILE_root && !dir->vd->show_system_files) ntfs_end;
    
    /* Convert the entry name from UTF-16LE into our current locale (UTF-8). */
    if (ntfs_ucstombs(name, name_len, &entry_name, 0) <= 0)
    {
        _errno = errno;
        ntfs_end;
    }
    
    /* Skip parent directory entry if we're currently at the root directory. */
    if (dir->first && dir->first->mref == FILE_root && MREF(mref) == FILE_root && strcmp(entry_name, NTFS_ENTRY_NAME_PARENT) == 0) ntfs_end;
    
    /* Check if this isn't the current/parent directory entry. */
    if (strcmp(entry_name, NTFS_ENTRY_NAME_SELF) != 0 && strcmp(entry_name, NTFS_ENTRY_NAME_PARENT) != 0)
    {
        /* Open entry. */
        ni = ntfs_pathname_to_inode(dir->vd->vol, dir->ni, entry_name);
        if (!ni)
        {
            _errno = errno;
            ntfs_end;
        }
        
        /* Skip system/hidden files depending on the mount flags. */
        if (((ni->flags & FILE_ATTR_HIDDEN) && !dir->vd->show_hidden_files) || ((ni->flags & FILE_ATTR_SYSTEM) && !dir->vd->show_system_files)) ntfs_end;
    }
    
    USBHSFS_LOG("Found directory entry mref %lu: \"%s\".", mref, entry_name);
    
    /* Allocate a new directory entry. */
    entry = malloc(sizeof(ntfs_dir_entry));
    if (!entry)
    {
        _errno = ENOMEM;
        ntfs_end;
    }
    
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
    if (ni) ntfs_inode_close(ni);
    
    /* Cleanup if we failed to enumerate the entry. */
    if (ntfs_ended_with_error)
    {
        if (entry_name) free(entry_name);
        if (entry) free(entry);
    }
    
    ntfs_return(0);
}
