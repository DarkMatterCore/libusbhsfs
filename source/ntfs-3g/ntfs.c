/*
 * ntfs.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 * This file is based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#include <sys/iosupport.h>
#include <sys/param.h>

#include "ntfs.h"

/* Type definitions. */

/// NTFS path.
typedef struct _ntfs_path {
    const char *path;               ///< Volume path (e.g. '/foo/bar/file.txt').
    const char *dir;                ///< Directory path (e.g. '/foo/bar').
    const char *name;               ///< Filename (e.g. 'file.txt').
    char buf[USB_MAX_PATH_LENGTH];  ///< Internal buffer containing the path string.
} ntfs_path;

/* Function prototypes. */

static ntfs_inode *ntfs_inode_open_from_path_reparse(ntfs_vd *vd, const char *path, int reparse_depth);

static void ntfs_split_path(const char *path, ntfs_path *p);

#ifdef DEBUG
int ntfs_log_handler_usbhsfs(const char *function, const char *file, int line, u32 level, void *data, const char *format, va_list args)
{
	(void)data;
    
    int ret = 0;
    size_t formatted_str_len = 0;
    char *formatted_str = NULL;
    
    /* Get formatted string length. */
    formatted_str_len = vsnprintf(NULL, 0, format, args);
    if (!formatted_str_len) return ret;
    
    /* Allocate buffer for the formatted string. */
    formatted_str = calloc(++formatted_str_len, sizeof(char));
    if (!formatted_str) return ret;
    
    /* Generate formatted string and save it to the logfile. */
    ret = (int)vsnprintf(formatted_str, formatted_str_len, format, args);
    if (ret)
    {
        /* Remove CRLFs and dots - we take care of them. */
        if (formatted_str[formatted_str_len - 1] == '\n') formatted_str[--formatted_str_len] = '\0';
        if (formatted_str[formatted_str_len - 1] == '\r') formatted_str[--formatted_str_len] = '\0';
        if (formatted_str[formatted_str_len - 1] == '.') formatted_str[--formatted_str_len] = '\0';
        
        /* Log message. */
        usbHsFsUtilsWriteMessageToLogFile(function, "%s (file \"%s\", line %d, level 0x%X).", formatted_str, file, line, level);
    }
    
    /* Free allocated buffer. */
    free(formatted_str);
	
	return ret;
}
#endif  /* DEBUG */

ntfs_inode *ntfs_inode_open_from_path(ntfs_vd *vd, const char *path)
{
    return ntfs_inode_open_from_path_reparse(vd, path, 1);
}

ntfs_inode *ntfs_inode_create(ntfs_vd *vd, const char *path, mode_t type, const char *target)
{
    ntfs_path full_path = {0};
    ntfs_inode *dir_ni = NULL, *ni = NULL;
    ntfschar *uname = NULL, *utarget = NULL;
    int uname_len = 0, utarget_len = 0;
    
    /* Safety check. */
    if (!vd || !vd->vol || !vd->root || !path || !*path || (type == S_IFLNK && (!target || !*target)))
    {
        errno = EINVAL;
        goto end;
    }
    
    /* TO DO: check if both paths belong to the same device. */
    /*if (type == S_IFLNK && vd != ntfs_volume_from_pathname(target))
    {
        errno = EXDEV;
        goto end;
    }*/
    
    /* Split entry path. */
    ntfs_split_path(path, &full_path);
    
    if (!strcmp(full_path.dir, NTFS_ENTRY_NAME_SELF))
    {
        /* We resolved to the root directory. */
        /* Make sure we have a valid entry name to work with. */
        if (full_path.name[0] == '\0')
        {
            errno = EINVAL;
            goto end;
        }
        
        /* Set parent directory inode. */
        dir_ni = vd->root;
    } else {
        /* Open the parent directory the desired entry will be created in. */
        dir_ni = ntfs_inode_open_from_path(vd, full_path.dir);
        if (!dir_ni) goto end;
    }
    
    /* Convert the entry name string from our current locale (UTF-8) into UTF-16LE. */
    uname_len = ntfs_mbstoucs(full_path.name, &uname);
    if (uname_len <= 0)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Create the new entry. */
    switch(type)
    {
        case S_IFDIR:   /* Directory. */
        case S_IFREG:   /* File. */
            USBHSFS_LOG("Creating inode in directory \"%s\" named \"%s\".", full_path.dir, full_path.name);
            ni = ntfs_create(dir_ni, 0, uname, uname_len, type);
            break;
        case S_IFLNK:   /* Symbolic link. */
            /* Convert the target link path string from our current locale (UTF-8) into UTF-16LE. */
            utarget_len = ntfs_mbstoucs(target, &utarget);
            if (utarget_len <= 0)
            {
                errno = EINVAL;
                goto end;
            }
            
            USBHSFS_LOG("Creating symlink in directory \"%s\" named \"%s\" targetting \"%s\".", full_path.dir, full_path.name, target);
            ni = ntfs_create_symlink(dir_ni, 0, uname, uname_len, utarget, utarget_len);
            break;
        default:        /* Invalid entry. */
            errno = EINVAL;
            break;
    }
    
end:
    if (utarget) free(utarget);
    
    if (uname) free(uname);
    
    if (dir_ni && dir_ni != vd->root) ntfs_inode_close(dir_ni);
    
    return ni;
}

int ntfs_inode_link(ntfs_vd *vd, const char *old_path, const char *new_path)
{
    ntfs_path full_old_path = {0}, full_new_path = {0};
    ntfs_inode *ni = NULL, *dir_ni = NULL;
    ntfschar *uname = NULL;
    int ret = -1, uname_len = 0;
    
    /* Safety check. */
    if (!vd || !vd->vol || !vd->root || !old_path || !*old_path || !new_path || !*new_path)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* TO DO: check if both paths belong to the same device. */
    /*if (vd != ntfs_volume_from_pathname(new_path))
    {
        errno = EXDEV;
        goto end;
    }*/
    
    /* Split entry paths. */
    ntfs_split_path(old_path, &full_old_path);
    ntfs_split_path(new_path, &full_new_path);
    
    /* Check if we resolved to the root directory. */
    if (!strcmp(full_old_path.dir, NTFS_ENTRY_NAME_SELF) && full_old_path.name[0] == '\0')
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Open the entry we will create a symlink for. */
    ni = ntfs_inode_open_from_path(vd, full_old_path.path);
    if (!ni) goto end;
    
    /* Get new parent directory inode. */
    if (!strcmp(full_new_path.dir, NTFS_ENTRY_NAME_SELF))
    {
        /* We resolved to the root directory. */
        /* Make sure we have a valid entry name to work with. */
        if (full_new_path.name[0] == '\0')
        {
            errno = EINVAL;
            goto end;
        }
        
        /* Set parent directory inode. */
        dir_ni = vd->root;
    } else {
        /* Open the new parent directory. */
        dir_ni = ntfs_inode_open_from_path(vd, full_new_path.dir);
        if (!dir_ni) goto end;
    }
    
    /* Convert the entry name string from our current locale (UTF-8) into UTF-16LE. */
    uname_len = ntfs_mbstoucs(full_new_path.name, &uname);
    if (uname_len <= 0)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Link the entry to its new parent directory. */
    USBHSFS_LOG("Linking inode \"%s\" to \"%s\".", full_old_path.path, full_new_path.path);
    ret = ntfs_link(ni, dir_ni, uname, uname_len);
    
end:
    if (uname) free(uname);
    
    if (dir_ni && dir_ni != vd->root) ntfs_inode_close(dir_ni);
    
    if (ni) ntfs_inode_close(ni);
    
    return ret;
}

int ntfs_inode_unlink(ntfs_vd *vd, const char *path)
{
    ntfs_path full_path = {0};
    ntfs_inode *ni = NULL, *dir_ni = NULL;
    ntfschar *uname = NULL;
    int ret = -1, uname_len = 0;
    
    /* Safety check. */
    if (!vd || !vd->vol || !vd->root || !path || !*path)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Split entry path. */
    ntfs_split_path(path, &full_path);
    
    /* Open entry. */
    ni = ntfs_inode_open_from_path(vd, full_path.path);
    if (!ni) goto end;
    
    /* Get the parent directory inode. */
    if (!strcmp(full_path.dir, NTFS_ENTRY_NAME_SELF))
    {
        /* We resolved to the root directory. */
        /* Make sure we have a valid entry name to work with. */
        if (full_path.name[0] == '\0')
        {
            errno = EINVAL;
            goto end;
        }
        
        /* Open root directory. */
        dir_ni = ntfs_inode_open(vd->vol, FILE_root);
    } else {
        /* Open the parent directory. */
        dir_ni = ntfs_inode_open_from_path(vd, full_path.dir);
    }
    
    if (!dir_ni) goto end;
    
    /* Convert the entry name string from our current locale (UTF-8) into UTF-16LE. */
    uname_len = ntfs_mbstoucs(full_path.name, &uname);
    if (uname_len <= 0)
    {
        errno = EINVAL;
        goto end;
    }
    
    USBHSFS_LOG("Unlinking inode \"%s\" from \"%s\".", full_path.name, full_path.dir);
    
    /* Unlink entry from its parent. */
    /* 'ni' and 'dir_ni' are always closed by ntfs_delete(), even if it fails. */
    ret = ntfs_delete(vd->vol, full_path.path, ni, dir_ni, uname, uname_len);
    
    ni = NULL;
    dir_ni = NULL;
    
end:
    if (uname) free(uname);
    
    if (dir_ni) ntfs_inode_close(dir_ni);
    
    if (ni) ntfs_inode_close(ni);
    
    return ret;
}

void ntfs_inode_update_times_filtered(ntfs_vd *vd, ntfs_inode *ni, ntfs_time_update_flags mask)
{
    if (!vd || !ni) return;
    
    /* Run the access time update strategy against the volume settings first. */
    if (vd->atime == ATIME_DISABLED) mask &= ~NTFS_UPDATE_ATIME;
    
    /* Update entry times. */
    if (mask) ntfs_inode_update_times(ni, mask);
}

static ntfs_inode *ntfs_inode_open_from_path_reparse(ntfs_vd *vd, const char *path, int reparse_depth)
{
    ntfs_inode *ni = NULL;
    char *target = NULL;
    
    /* Safety check. */
    if (!vd || !vd->vol || !vd->root || !path || !*path || reparse_depth <= 0 || reparse_depth > NTFS_MAX_SYMLINK_DEPTH)
    {
        errno = EINVAL;
        goto end;
    }
    
    USBHSFS_LOG("Opening requested inode \"%s\" (reparse depth %d).", path, reparse_depth);
    
    /* Open requested inode. */
    ni = ntfs_pathname_to_inode(vd->vol, vd->root, path);
    if (!ni)
    {
        USBHSFS_LOG("Failed to open requested inode \"%s\" (errno %d).", path, errno);
        goto end;
    }
    
    USBHSFS_LOG("Successfully opened inode from path \"%s\" (mft_no %lu).", path, ni->mft_no);
    
    /* If the entry was found and it has reparse data, then resolve the true entry. */
    /* This effectively follows directory junctions and symbolic links until the target entry is found. */
    if ((ni->flags & FILE_ATTR_REPARSE_POINT) && ntfs_possible_symlink(ni))
    {
        /* Get the target path of this entry. */
        target = ntfs_make_symlink(ni, path);
        if (!target) goto end;
        
        /* Close this entry (we are no longer interested in it). */
        ntfs_inode_close(ni);
        
        /* Open the target entry. */
        USBHSFS_LOG("Following inode symlink \"%s\" -> \"%s\".", path, target);
        ni = ntfs_inode_open_from_path_reparse(vd, target, ++reparse_depth);
        
        /* Clean up. */
        free(target);
    }
    
end:
    return ni;
}

static void ntfs_split_path(const char *path, ntfs_path *p)
{
    USBHSFS_LOG("Input path: \"%s\".", path);
    
    /* Setup NTFS path. */
    memset(p, 0, sizeof(ntfs_path));
    p->path = path;
    
    /* Copy the path to internal buffer so we can modify it. */
    strcpy(p->buf, p->path);
    
    /* Split the path into separate directory and filename parts. */
    /* e.g. "/dir/file.txt" => dir: "/dir", name: "file.txt". */
    char *buf_sep = strrchr(p->buf, PATH_SEP);
    
    /* If there's just a single path separator at the start of the string, set the directory string to the current directory. */
    /* Otherwise, just use the directory string as-is. */
    if (buf_sep == p->buf)
    {
        p->dir = NTFS_ENTRY_NAME_SELF;
    } else {
        *buf_sep = '\0';
        p->dir = p->buf;
    }
    
    p->name = (buf_sep + 1);
    
    USBHSFS_LOG("Output strings -> Path: \"%s\" | Directory: \"%s\" | Name: \"%s\".", p->path, p->dir, p->name);
}
