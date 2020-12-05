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
#include "ntfs_dev.h"

#include "../usbhsfs_utils.h"
#include "../usbhsfs_manager.h"
#include "../usbhsfs_drive.h"
#include "../usbhsfs_mount.h"

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
    .chmod_r      = ntfsdev_chmod,
    .fchmod_r     = ntfsdev_fchmod,
    .rmdir_r      = ntfsdev_rmdir,
    .lstat_r      = ntfsdev_stat,
    .utimes_r     = ntfsdev_utimes
};

const devoptab_t *ntfsdev_get_devoptab()
{
    return &ntfsdev_devoptab;
}

#define ntfs_end                        goto end;
#define ntfs_error_with_code(x)         r->_errno = x; ntfs_end;

#define ntfs_declare_vol_state          ntfs_vd *vd = ((UsbHsFsDriveLogicalUnitFileSystemContext*) r->deviceData)->ntfs;
#define ntfs_declare_file_state         ntfs_file_state *file = ((ntfs_file_state*) fd);
#define ntfs_declare_dir_state          ntfs_file_state *dir = ((ntfs_dir_state*) dirState);

#define ntfs_lock_drive_ctx             UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*) r->deviceData; \
                                        UsbHsFsDriveContext *drive_ctx = ntfsdev_get_drive_ctx_and_lock(&fs_ctx); \
                                        if (!drive_ctx) \
                                        { \
                                            ntfs_error_with_code(ENODEV); \
                                        }

#define ntfs_unlock_drive_ctx           if (drive_ctx) mutexUnlock(&(drive_ctx->mutex))

#define ntfs_return(x)                  return (r->_errno == 0) ? x : -1

static UsbHsFsDriveContext *ntfsdev_get_drive_ctx_and_lock(UsbHsFsDriveLogicalUnitFileSystemContext **fs_ctx)
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

int ntfsdev_open (struct _reent *r, void *fd, const char *path, int flags, int mode)
{
    int ret = 0;
    ntfs_log_trace("fileStruct %p, path \"%s\", flags %i, mode %i", (void *) fd, path, flags, mode);
    ntfs_declare_vol_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* Check access mode. */
    switch(flags & O_ACCMODE)
    {
        case O_RDONLY:  /* Read-only. Don't allow append flag. */
            if (flags & O_APPEND)
            {
                ntfs_error_with_code(EINVAL);
            }
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
            ntfs_error_with_code(EINVAL);
    }

    /* Set the file volume descriptor. */
    file->vd = vd;
    if (!file->vd)
    {
        ntfs_error_with_code(ENODEV);
    }

    /* Set the file node descriptor and ensure that it is actually a file. */
    file->ni = ntfs_inode_open_pathname(file->vd, path);
    if (file->ni && (file->ni->mrec->flags & MFT_RECORD_IS_DIRECTORY))
    {
        ntfs_error_with_code(EISDIR);
    }

    /* Are we creating this file? */
    if ((flags & O_CREAT))
    {
        /* Create + exclusive when the file already exists is not allowed */
        if ((flags & O_EXCL) && file->ni)
        {
            ntfs_error_with_code(EEXIST)
        }
        
        /* Create the file if it doesn't exist yet */
        else if (!file->ni)
        {
            file->ni = ntfs_inode_create(file->vd, path, S_IFREG, NULL);
            if (!file->ni)
            {
                ntfs_error_with_code(errno);
            }
        }  
    }

    /* Sanity check, the file should be open by now. */
    if (!file->ni)
    {
        ntfs_error_with_code(ENOENT);
    }

    /* Open the files data attribute. */
    file->data = ntfs_attr_open(file->ni, AT_DATA, AT_UNNAMED, 0);
    if(!file->data)
    {
        ntfs_error_with_code(errno);
    }

    /* Determine if this files data is compressed and/or encrypted. */
    file->compressed = NAttrCompressed(file->data) || (file->ni->flags & FILE_ATTR_COMPRESSED);
    file->encrypted = NAttrEncrypted(file->data) || (file->ni->flags & FILE_ATTR_ENCRYPTED);

    /* We cannot read/write encrypted files. */
    if (file->encrypted)
    {
        ntfs_error_with_code(EACCES);
    }

    /* Make sure we aren't trying to write to a read-only file. */
    if ((file->ni->flags & FILE_ATTR_READONLY) && file->write)
    {
        ntfs_error_with_code(EROFS);
    }

    /* Truncate the file if requested. */
    if ((flags & O_TRUNC) && file->write)
    {
        if (ntfs_attr_truncate(file->data, 0))
        {
            ntfs_error_with_code(errno);
        }
    }

    /* Set the files current position and length. */
    file->pos = 0;
    file->len = file->data->data_size;

    /* Update file last access time. */
    ntfs_update_times(file->vd, file->ni, NTFS_UPDATE_ATIME);

    ret = (file->ni && file->data);

end:

    /* If the file failed to open, clean-up */
    if (r->_errno)
    {
        if (file && file->data)
        {
            ntfs_attr_close(file->data);
            file->data = NULL;
        }
        if (file && file->ni)
        {
            ntfs_inode_close(file->ni);
            file->ni = NULL;
        }
    }
    
    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_close (struct _reent *r, void *fd)
{
    int ret = 0;
    ntfs_log_trace("fd %p", fd);
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* If the file is dirty, sync it (and attributes). */
    if(NInoDirty(file->ni))
    {
        ntfs_inode_sync(file->ni);
    }

    /* Special case clean-ups compressed and/or encrypted files. */
    if (file->compressed && file->data)
    {
        ntfs_attr_pclose(file->data);
    }
#ifdef HAVE_SETXATTR
    if (file->encrypted && file->data)
    {
        ntfs_efs_fixup_attribute(NULL, file->data);
    }
#endif

    /* Close the file data attribute. */
    if (file->data)
    {
        ntfs_attr_close(file->data);
    }

    /* Close the file node. */
    if (file->ni)
    {
        ntfs_inode_close(file->ni);
    }

    /* Reset the file state. */
    memset(file, 0, sizeof(ntfs_file_state));

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

ssize_t ntfsdev_write (struct _reent *r, void *fd, const char *ptr, size_t len)
{
    ssize_t ret = 0;
    off_t original_pos = 0;
    bool original_pos_must_be_restored = false;
    ntfs_log_trace("fd %p, ptr %p, len %lu", fd, ptr, len);
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* Short circuit. */
    if (!ptr || len <= 0)
    {
        ntfs_end;
    }

    /* Check that we are allowed to write to this file. */
    if (!file->write)
    {
        ntfs_error_with_code(EACCES);
    }

    /* If we are in append mode, backup the current position and move to the end of the file. */
    if (file->append)
    {
        original_pos_must_be_restored = true;
        original_pos = file->pos;
        file->pos = file->len;
    }

    /* Write to the files data atrribute length is satisfied. */
    while (len)
    {
        ssize_t written = ntfs_attr_pwrite(file->data, file->pos, len, ptr);
        if (written <= 0 || written > len)
        {
            ntfs_error_with_code(errno);
        }
        ret += written;
        ptr += written;
        len -= written;
        file->pos += written;
    }

end:

    /* If we are in append mode, restore the current position to were it was originally. */
    if (file && original_pos_must_be_restored)
    {
        file->pos = original_pos;
    }

    /* Did we end up writing anything? */
    if (file && ret > 0)
    {
        /* Mark the file as dirty. */
        NInoSetDirty(file->ni);

        /* Mark the file for archiving. */
        file->ni->flags |= FILE_ATTR_ARCHIVE;
        
        /* Update file last access and modify times. */
        ntfs_update_times(file->vd, file->ni, NTFS_UPDATE_AMCTIME);
        
        /* Update the files data length. */
        file->len = file->data->data_size;
    }

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

ssize_t ntfsdev_read (struct _reent *r, void *fd, char *ptr, size_t len)
{
    ssize_t ret = 0;
    ntfs_log_trace("fd %p, ptr %p, len %lu", fd, ptr, len);
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* Short circuit. */
    if (!ptr || len <= 0)
    {
        ntfs_end;
    }

    /* Check that we are allowed to read from this file. */
    if (!file->read)
    {
        ntfs_error_with_code(EACCES);
    }

    /* Don't read past the end of file. */
    if (file->pos + len > file->len)
    {
        //ntfs_error_with_code(EOVERFLOW);
        ntfs_log_trace("EOVERFLOW detected, clamping to maximum available length and continuing");
        r->_errno = EOVERFLOW;
        memset(ptr, 0, len);
        len = file->len - file->pos;
    }

    /* Read from the files data attribute until length is satisfied. */
    while (len)
    {
        ssize_t read = ntfs_attr_pread(file->data, file->pos, len, ptr);
        if (read <= 0 || read > len)
        {
            ntfs_error_with_code(errno);
        }
        ret += read;
        ptr += read;
        len -= read;
        file->pos += read;
    }

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

off_t ntfsdev_seek (struct _reent *r, void *fd, off_t pos, int dir)
{
    off_t ret = 0;
    ntfs_log_trace("fd %p, pos %li, dir %i", fd, pos, dir);
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* Find the offset to seek from. */
    switch(dir)
    {
        /* Set absolute position relative to zero (start offset). */
        case SEEK_SET: file->pos = MIN(MAX(pos, 0), file->len); break;
        /* Set position relative to the current position. */
        case SEEK_CUR: file->pos = MIN(MAX(file->pos + pos, 0), file->len); break;
        /* Set position relative to EOF. */
        case SEEK_END: file->pos = MIN(MAX(file->len + pos, 0), file->len); break;
        /* Invalid option. */
        default: ntfs_error_with_code(EINVAL);
    }

    ret = file->pos;

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_fstat (struct _reent *r, void *fd, struct stat *st)
{
    int ret = 0;
    ntfs_log_trace("fd %p", fd);
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* Short circuit. */
    if (!st)
    {
        ntfs_end;
    }

    /* Get the file stats. */
    ret = ntfs_stat(file->vd, file->ni, st);
    if (ret)
    {
        ntfs_error_with_code(errno);
    }

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_stat (struct _reent *r, const char *path, struct stat *st)
{
    ntfs_log_trace("path \"%s\", st %p", path, st);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

int ntfsdev_link (struct _reent *r, const char *existing, const char *newLink)
{
    ntfs_log_trace("existing \"%s\", newLink \"%s\"", existing, newLink);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

int ntfsdev_unlink (struct _reent *r, const char *name)
{
    ntfs_log_trace("name \"%s\"", name);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

int ntfsdev_chdir (struct _reent *r, const char *name)
{
    ntfs_log_trace("name \"%s\"", name);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

int ntfsdev_rename (struct _reent *r, const char *oldName, const char *newName)
{
    ntfs_log_trace("oldName \"%s\", newName \"%s\"", oldName, newName);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

int ntfsdev_mkdir (struct _reent *r, const char *path, int mode)
{
    ntfs_log_trace("path \"%s\", mode %i", path, mode);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

DIR_ITER *ntfsdev_diropen (struct _reent *r, DIR_ITER *dirState, const char *path)
{
    ntfs_log_trace("dirState %p, path \"%s\"", dirState, path);

    // TODO: This...
    errno = ENOTSUP;
    return NULL;
}

int ntfsdev_dirreset (struct _reent *r, DIR_ITER *dirState)
{
    ntfs_log_trace("dirState %p", dirState);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

int ntfsdev_dirnext (struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    ntfs_log_trace("dirState %p, filename %p, filestat %p", dirState, filename, filestat);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

int ntfsdev_dirclose (struct _reent *r, DIR_ITER *dirState)
{
    ntfs_log_trace("dirState %p", dirState);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

int ntfsdev_statvfs (struct _reent *r, const char *path, struct statvfs *buf)
{
    ntfs_log_trace("path \"%s\", buf %p", path, buf);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

int ntfsdev_ftruncate (struct _reent *r, void *fd, off_t len)
{
    int ret = 0;
    ntfs_log_trace("fd %p, len %lu", fd, (u64) len);
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* Make sure length is non-negative. */
    if (len < 0)
    {
        ntfs_error_with_code(EINVAL);
    }
    
    /* Check that we are allowed to write to this file. */
    if (!file->write)
    {
        ntfs_error_with_code(EACCES);
    }

    /* For compressed files, only deleting and expanding contents are implemented. */
    if (file->compressed && len > 0 && len < file->data->initialized_size) 
    {
        ntfs_error_with_code(EOPNOTSUPP);
    }

    /* Resize the files data attribute, either by expanding or truncating. */
    if (len > file->data->initialized_size)
    {
        char zero = 0;
        if (ntfs_attr_pwrite(file->data, len - 1, 1, &zero) <= 0)
        {
            ntfs_error_with_code(errno);
        }
    } 
    else 
    {
        if (ntfs_attr_truncate(file->data, len))
        {
            ntfs_error_with_code(errno);
        }
    }

end:

    /* Did the file size actually change? */
    if (file && file->len != file->data->data_size)
    {
        /* Mark the file as dirty. */
        NInoSetDirty(file->ni);

        /* Mark the file for archiving. */
        file->ni->flags |= FILE_ATTR_ARCHIVE;
        
        /* Update file last access and modify times. */
        ntfs_update_times(file->vd, file->ni, NTFS_UPDATE_AMCTIME);
        
        /* Update the files data length. */
        file->len = file->data->data_size;
    }

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_fsync (struct _reent *r, void *fd)
{
    int ret = 0;
    ntfs_log_trace("fd %p", fd);
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* Sync the file (and attributes). */
    ret = ntfs_inode_sync(file->ni);
    if (ret)
    {
        ntfs_error_with_code(errno);
    }

    /* Mark the file as no longer dirty. */
    NInoClearDirty(file->ni);

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_chmod (struct _reent *r, const char *path, mode_t mode)
{
    ntfs_log_trace("path \"%s\", mode %i", path, mode);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

int ntfsdev_fchmod (struct _reent *r, void *fd, mode_t mode)
{
    int ret = 0;
    ntfs_log_trace("fd %p, mode %i", fd, mode);
    ntfs_lock_drive_ctx;

    // TODO: Consider implementing this...
    /*
    SECURITY_CONTEXT sxc;
    ntfs_set_mode(&scx, file->ni, mode);
    */
   ntfs_error_with_code(ENOTSUP);

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_rmdir (struct _reent *r, const char *name)
{
    ntfs_log_trace("name \"%s\"", name);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}

int ntfsdev_utimes (struct _reent *r, const char *filename, const struct timeval times[2])
{
    ntfs_log_trace("filename \"%s\", time[0] %li, time[1] %li", filename, times[0].tv_sec, times[1].tv_sec);

    // TODO: This...
    errno = ENOTSUP;
    return -1;
}
