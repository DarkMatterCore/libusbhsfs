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
#define ntfs_error(x)                   r->_errno = _errno = x; ntfs_end;
#define ntfs_ended_with_error           (_errno == 0)

#define ntfs_declare_error_state        int _errno = 0; 
#define ntfs_declare_vol_state          ntfs_vd *vd = ((UsbHsFsDriveLogicalUnitFileSystemContext*) r->deviceData)->ntfs;
#define ntfs_declare_file_state         ntfs_file_state *file = ((ntfs_file_state*) fd);
#define ntfs_declare_dir_state          ntfs_file_state *dir = ((ntfs_dir_state*) dirState);

#define ntfs_lock_drive_ctx             UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = (UsbHsFsDriveLogicalUnitFileSystemContext*) r->deviceData; \
                                        UsbHsFsDriveContext *drive_ctx = ntfsdev_get_drive_ctx_and_lock(&fs_ctx); \
                                        if (!drive_ctx) \
                                        { \
                                            ntfs_error(ENODEV); \
                                        }

#define ntfs_unlock_drive_ctx           if (drive_ctx) mutexUnlock(&(drive_ctx->mutex))

#define ntfs_return(x)                  return (ntfs_ended_with_error) ? x : -1

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
    ntfs_declare_error_state;
    ntfs_declare_vol_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* Check access mode. */
    switch(flags & O_ACCMODE)
    {
        /* Read-only. Don't allow append flag. */
        case O_RDONLY:
        {
            if (flags & O_APPEND)
            {
                ntfs_error(EINVAL);
            }
            file->read = true;
            file->write = false;
            file->append = false;
            break;
        }

        /* Write-only. */
        case O_WRONLY:
        {
            file->read = false;
            file->write = true;
            file->append = (flags & O_APPEND);
            break;
        }

        /* Read and write. */
        case O_RDWR:
        {
            file->read = true;
            file->write = true;
            file->append = (flags & O_APPEND);
            break;
        }

        /* Invalid option. */
        default:
        {
            ntfs_error(EINVAL);
        }
    }

    /* Set the file volume descriptor. */
    file->vd = vd;
    if (!file->vd)
    {
        ntfs_error(ENODEV);
    }

    /* Set the file node descriptor and ensure that it is actually a file. */
    file->ni = ntfs_inode_open_from_path(file->vd, path);
    if (file->ni && (file->ni->mrec->flags & MFT_RECORD_IS_DIRECTORY))
    {
        ntfs_error(EISDIR);
    }

    /* Are we creating this file? */
    if ((flags & O_CREAT))
    {
        /* Create + exclusive when the file already exists is not allowed */
        if ((flags & O_EXCL) && file->ni)
        {
            ntfs_error(EEXIST)
        }
        
        /* Create the file if it doesn't exist yet */
        else if (!file->ni)
        {
            file->ni = ntfs_inode_create(file->vd, path, S_IFREG, NULL);
            if (!file->ni)
            {
                ntfs_error(errno);
            }
        }  
    }

    /* Sanity check, the file should be open by now. */
    if (!file->ni)
    {
        ntfs_error(ENOENT);
    }

    /* Open the files data attribute. */
    file->data = ntfs_attr_open(file->ni, AT_DATA, AT_UNNAMED, 0);
    if(!file->data)
    {
        ntfs_error(errno);
    }

    /* Determine if this files data is compressed and/or encrypted. */
    file->compressed = NAttrCompressed(file->data) || (file->ni->flags & FILE_ATTR_COMPRESSED);
    file->encrypted = NAttrEncrypted(file->data) || (file->ni->flags & FILE_ATTR_ENCRYPTED);

    /* We cannot read/write encrypted files. */
    if (file->encrypted)
    {
        ntfs_error(EACCES);
    }

    /* Make sure we aren't trying to write to a read-only file. */
    if ((file->ni->flags & FILE_ATTR_READONLY) && file->write)
    {
        ntfs_error(EROFS);
    }

    /* Truncate the file if requested. */
    if ((flags & O_TRUNC) && file->write)
    {
        if (ntfs_attr_truncate(file->data, 0))
        {
            ntfs_error(errno);
        }
    }

    /* Set the files current position and length. */
    file->pos = 0;
    file->len = file->data->data_size;

    /* Update file last access time. */
    ntfs_inode_update_times_filtered(file->vd, file->ni, NTFS_UPDATE_ATIME);

    ret = (file->ni && file->data);

end:

    /* If the file failed to open, clean-up */
    if (ntfs_ended_with_error)
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
    ntfs_log_trace("fd %p", fd);
    ntfs_declare_error_state;
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
    ntfs_return(0);
}

ssize_t ntfsdev_write (struct _reent *r, void *fd, const char *ptr, size_t len)
{
    ssize_t ret = 0;
    off_t original_pos = 0;
    bool original_pos_must_be_restored = false;
    ntfs_log_trace("fd %p, ptr %p, len %lu", fd, ptr, len);
    ntfs_declare_error_state;
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
        ntfs_error(EACCES);
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
            ntfs_error(errno);
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
        ntfs_inode_update_times_filtered(file->vd, file->ni, NTFS_UPDATE_AMCTIME);
        
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
    ntfs_declare_error_state;
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
        ntfs_error(EACCES);
    }

    /* Don't read past the end of file. */
    if (file->pos + len > file->len)
    {
        //ntfs_error(EOVERFLOW);
        ntfs_log_trace("EOVERFLOW detected, clamping to maximum available length and continuing (filepos %li, filelen %li)", file->pos, file->len);
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
            ntfs_error(errno);
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
    ntfs_declare_error_state;
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
        default: ntfs_error(EINVAL);
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
    ntfs_declare_error_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* Short circuit. */
    if (!st)
    {
        ntfs_end;
    }

    /* Get the file stats. */
    ret = ntfs_inode_stat(file->vd, file->ni, st);
    if (ret)
    {
        ntfs_error(errno);
    }

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_stat (struct _reent *r, const char *path, struct stat *st)
{
    int ret = 0;
    ntfs_inode *ni = NULL;
    ntfs_log_trace("path \"%s\", st %p", path, st);
    ntfs_declare_error_state;
    ntfs_declare_vol_state;
    ntfs_lock_drive_ctx;

    /* Short circuit. */
    if (!st)
    {
        ntfs_end;
    }

    /* Short circuit for current/parent directory alias'. */
    if(strcmp(path, ".") == 0 || strcmp(path, "..") == 0)
    {
        memset(st, 0, sizeof(struct stat));
        st->st_mode = S_IFDIR;
        ntfs_end;
    }

    /* Get the entry. */
    ni = ntfs_inode_open_from_path(vd, path);
    if (!ni) {
        ntfs_error(errno);
    }

    /* Get the entry stats. */
    ret = ntfs_inode_stat(vd, ni, st);
    if (ret)
    {
        ntfs_error(errno);
    }

end:

    /* Clean-up. */
    if (ni) 
    {
        ntfs_inode_close(ni);
    }

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_link (struct _reent *r, const char *existing, const char *newLink)
{
    int ret = 0;
    ntfs_inode *ni = NULL;
    ntfs_log_trace("existing \"%s\", newLink \"%s\"", existing, newLink);
    ntfs_declare_error_state;
    ntfs_declare_vol_state;
    ntfs_lock_drive_ctx;

    /* Create a symbolic link entry joining the two paths */
    ni = ntfs_inode_create(vd, existing, S_IFLNK, newLink);
    if (!ni)
    {
        ntfs_error(errno);
    }

end:

    /* Clean-up. */
    if (ni) 
    {
        ntfs_inode_close(ni);
    }

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_unlink (struct _reent *r, const char *name)
{
    int ret = 0;
    ntfs_log_trace("name \"%s\"", name);
    ntfs_declare_error_state;
    ntfs_declare_vol_state;
    ntfs_lock_drive_ctx;

    /* Unlink the entry. */
    if (ntfs_inode_unlink(vd, name))
    {
        ntfs_error(errno);
    }

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_chdir (struct _reent *r, const char *name)
{
    int ret = 0;
    ntfs_inode *ni = NULL, *old_cwd = NULL;
    ntfs_log_trace("name \"%s\"", name);
    ntfs_declare_error_state;
    ntfs_declare_vol_state;
    ntfs_lock_drive_ctx;

    /* Find the directory entry */
    ni = ntfs_inode_open_from_path(vd, name);
    if (!ni)
    {
        ntfs_error(ENOENT);
    }

    /* Ensure the entry is indeed a directory */
    if (!(ni->mrec->flags && MFT_RECORD_IS_DIRECTORY))
    {
        ntfs_error(ENOTDIR);
    }

    /* Swap current directories */
    old_cwd = vd->cwd;
    vd->cwd = ni;
    ni = old_cwd;

end:

    /* Clean-up. */
    if (ni) 
    {
        ntfs_inode_close(ni);
    }

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_rename (struct _reent *r, const char *oldName, const char *newName)
{
    int ret = 0;
    ntfs_log_trace("oldName \"%s\", newName \"%s\"", oldName, newName);
    ntfs_declare_error_state;
    ntfs_declare_vol_state;
    ntfs_lock_drive_ctx;

    /* You cannot link entries across devices */
    if (oldName && newName)
    {
        /* TODO: Check that both paths belong to the same device.
        if (vd != ntfs_volume_from_pathname(newName))
        {
            errno = EXDEV;
            goto end;
        }
        */
    }

    /* Check that there is no entry with the new name already */
    ntfs_inode *ni = ntfs_inode_open_from_path(vd, newName);
    if (ni)
    {
        ntfs_error(EEXIST);
    }
    else
    {
        /* Close it immediately, we don't actually need it. */
        ntfs_inode_close(ni);
    }
    

    /* Link the old entry with the new one. */
    if (ntfs_inode_link(vd, oldName, newName))
    {
        ntfs_error(errno);
    }

    /* Unlink the old entry. */
    if (ntfs_inode_unlink(vd, oldName))
    {
        ntfs_error(errno);
    }

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_mkdir (struct _reent *r, const char *path, int mode)
{
    int ret = 0;
    ntfs_inode *ni = NULL;
    ntfs_log_trace("path \"%s\", mode %i", path, mode);
    ntfs_declare_error_state;
    ntfs_declare_vol_state;
    ntfs_lock_drive_ctx;

    /* Create the directory entry */
    ni = ntfs_inode_create(vd, path, S_IFDIR, NULL);
    if (!ni) {
        ntfs_error(errno);
    }

end:

    /* Clean-up. */
    if (ni) 
    {
        ntfs_inode_close(ni);
    }

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
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
    int ret = 0;
    ntfs_log_trace("path \"%s\", buf %p", path, buf);
    ntfs_declare_error_state;
    ntfs_declare_vol_state;
    ntfs_lock_drive_ctx;

    /* Short circuit. */
    if (!buf)
    {
        ntfs_end;
    }

    /* Zero out the stat buffer. */
    memset(buf, 0, sizeof(struct statvfs));

    /* Check available free space. */
    if(ntfs_volume_get_free_space(vd->vol) < 0)
    {
        ntfs_error(ENOSPC);
    }

    /* File system sector size. */
    buf->f_bsize = vd->vol->cluster_size;

    /* Fundamental file system sector size. */
    buf->f_frsize = vd->vol->cluster_size;

    /* Total number of sectors in file system (in units of f_frsize). */
    buf->f_blocks = vd->vol->nr_clusters;

    /* Free sectors available for all and for non-privileged processes. */
    s64 size = MAX(vd->vol->free_clusters, 0);
    buf->f_bfree = buf->f_bavail = size;

    /* Free inodes within the free space. */
    int delta_bits = vd->vol->cluster_size_bits - vd->vol->mft_record_size_bits;
    if (delta_bits >= 0)
    {
        size <<= delta_bits;
    }
    else
    {
        size >>= -delta_bits;
    }

    /* Total number of inodes in file system. */
    buf->f_files = (vd->vol->mftbmp_na->allocated_size << 3) + size;

    /* Free inodes available for all and for non-privileged processes. */
    size += vd->vol->free_mft_records;
    buf->f_ffree = buf->f_favail = MAX(size, 0);

    /* File system id. */
    buf->f_fsid = vd->id;

    /* Bit mask of f_flag values. */
    buf->f_flag = (NVolReadOnly(vd->vol) ? ST_RDONLY : 0);

    /* Maximum length of file names. */
    buf->f_namemax = NTFS_MAX_NAME_LEN;

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_ftruncate (struct _reent *r, void *fd, off_t len)
{
    int ret = 0;
    ntfs_log_trace("fd %p, len %lu", fd, (u64) len);
    ntfs_declare_error_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* Make sure length is non-negative. */
    if (len < 0)
    {
        ntfs_error(EINVAL);
    }
    
    /* Check that we are allowed to write to this file. */
    if (!file->write)
    {
        ntfs_error(EACCES);
    }

    /* For compressed files, only deleting and expanding contents are implemented. */
    if (file->compressed && len > 0 && len < file->data->initialized_size) 
    {
        ntfs_error(EOPNOTSUPP);
    }

    /* Resize the files data attribute, either by expanding or truncating. */
    if (len > file->data->initialized_size)
    {
        char zero = 0;
        if (ntfs_attr_pwrite(file->data, len - 1, 1, &zero) <= 0)
        {
            ntfs_error(errno);
        }
    } 
    else 
    {
        if (ntfs_attr_truncate(file->data, len))
        {
            ntfs_error(errno);
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
        ntfs_inode_update_times_filtered(file->vd, file->ni, NTFS_UPDATE_AMCTIME);
        
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
    ntfs_declare_error_state;
    ntfs_declare_file_state;
    ntfs_lock_drive_ctx;

    /* Sync the file (and attributes). */
    ret = ntfs_inode_sync(file->ni);
    if (ret)
    {
        ntfs_error(errno);
    }

    /* Mark the file as no longer dirty. */
    NInoClearDirty(file->ni);

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_chmod (struct _reent *r, const char *path, mode_t mode)
{
    int ret = 0;
    ntfs_inode *ni = NULL;
    ntfs_log_trace("path \"%s\", mode %i", path, mode);
    ntfs_declare_error_state;
    ntfs_declare_vol_state;
    ntfs_lock_drive_ctx;

    /* Get the entry. */
    ni = ntfs_inode_open_from_path(vd, path);
    if (!ni) {
        ntfs_error(errno);
    }

    // TODO: Implement this...
    //SECURITY_CONTEXT sxc; /* need to build this using info from 'vd' */
    //ntfs_set_mode(&scx, ni, mode);
    ntfs_error(ENOTSUP);
   
end:

    /* Clean-up. */
    if (ni) 
    {
        ntfs_inode_close(ni);
    }

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_fchmod (struct _reent *r, void *fd, mode_t mode)
{
    int ret = 0;
    ntfs_log_trace("fd %p, mode %i", fd, mode);
    ntfs_declare_error_state;
    ntfs_lock_drive_ctx;

    // TODO: Implement this...
    //SECURITY_CONTEXT sxc; /* need to build this using info from 'vd' */
    //ntfs_set_mode(&scx, file->ni, mode);
    ntfs_error(ENOTSUP);

end:

    ntfs_unlock_drive_ctx;
    ntfs_return(ret);
}

int ntfsdev_rmdir (struct _reent *r, const char *name)
{
    ntfs_log_trace("name \"%s\"", name);
    // TODO: Check that there is nothing extra we need to do when unlinking directories
    return ntfsdev_unlink(r, name);
}

int ntfsdev_utimes (struct _reent *r, const char *filename, const struct timeval times[2])
{
    int ret = 0;
    ntfs_inode *ni = NULL;
    ntfs_log_trace("filename \"%s\", time[0] %li, time[1] %li", filename, times[0].tv_sec, times[1].tv_sec);
    ntfs_declare_error_state;
    ntfs_declare_vol_state;
    ntfs_lock_drive_ctx;

    /* Get the entry. */
    ni = ntfs_inode_open_from_path(vd, filename);
    if (!ni) {
        ntfs_error(errno);
    }
    
    // TODO: Implement this...
    //u64 values[2] = times /* how to convert these? */
    //ntfs_inode_set_times(ni, values, 2, NTFS_UPDATE_ATIME | NTFS_UPDATE_MTIME);
    ntfs_error(ENOTSUP);
    
end:

    /* Clean-up. */
    if (ni) 
    {
        ntfs_inode_close(ni);
    }

    ntfs_return(ret);
}
