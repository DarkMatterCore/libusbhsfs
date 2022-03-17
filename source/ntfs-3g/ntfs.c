/*
 * ntfs.c
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#include "ntfs.h"

/* Type definitions. */

/// NTFS path.
typedef struct _ntfs_path {
    const char *path;           ///< Volume path (e.g. '/foo/bar/file.txt').
    const char *dir;            ///< Directory path (e.g. '/foo/bar').
    const char *name;           ///< Filename (e.g. 'file.txt').
    char buf[MAX_PATH_LENGTH];  ///< Internal buffer containing the path string.
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
        usbHsFsLogWriteFormattedStringToLogFile(function, "%s (file \"%s\", line %d, level 0x%X).", formatted_str, file, line, level);
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
    if (!vd || !vd->vol || !path || !*path || (type == S_IFLNK && (!target || !*target)))
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Split entry path. */
    ntfs_split_path(path, &full_path);
    
    /* Make sure we have a valid entry name to work with. */
    if (full_path.name[0] == '\0')
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Open the parent directory the desired entry will be created in. */
    dir_ni = ntfs_inode_open_from_path(vd, full_path.dir);
    if (!dir_ni) goto end;
    
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
            USBHSFS_LOG_MSG("Creating inode in directory \"%s\" named \"%s\".", full_path.dir, full_path.name);
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
            
            USBHSFS_LOG_MSG("Creating symlink in directory \"%s\" named \"%s\" targetting \"%s\".", full_path.dir, full_path.name, target);
            ni = ntfs_create_symlink(dir_ni, 0, uname, uname_len, utarget, utarget_len);
            break;
        default:        /* Invalid entry. */
            errno = EINVAL;
            break;
    }
    
end:
    if (utarget) free(utarget);
    
    if (uname) free(uname);
    
    if (dir_ni) ntfs_inode_close(dir_ni);
    
    return ni;
}

int ntfs_inode_link(ntfs_vd *vd, const char *old_path, const char *new_path)
{
    ntfs_path full_old_path = {0}, full_new_path = {0};
    ntfs_inode *ni = NULL, *dir_ni = NULL;
    ntfschar *uname = NULL;
    int ret = -1, uname_len = 0;
    
    /* Safety check. */
    if (!vd || !vd->vol || !old_path || !*old_path || !new_path || !*new_path)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Split entry paths. */
    ntfs_split_path(old_path, &full_old_path);
    ntfs_split_path(new_path, &full_new_path);
    
    /* Make sure we have valid old and new entry names to work with. */
    if (full_old_path.name[0] == '\0' || full_new_path.name[0] == '\0')
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Open the entry we will create a symlink for. */
    ni = ntfs_inode_open_from_path(vd, full_old_path.path);
    if (!ni) goto end;
    
    /* Open new parent directory. */
    dir_ni = ntfs_inode_open_from_path(vd, full_new_path.dir);
    if (!dir_ni) goto end;
    
    /* Convert the entry name string from our current locale (UTF-8) into UTF-16LE. */
    uname_len = ntfs_mbstoucs(full_new_path.name, &uname);
    if (uname_len <= 0)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Link the entry to its new parent directory. */
    USBHSFS_LOG_MSG("Linking inode \"%s\" to \"%s\".", full_old_path.path, full_new_path.path);
    ret = ntfs_link(ni, dir_ni, uname, uname_len);
    
end:
    if (uname) free(uname);
    
    if (dir_ni) ntfs_inode_close(dir_ni);
    
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
    if (!vd || !vd->vol || !path || !*path)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Split entry path. */
    ntfs_split_path(path, &full_path);
    
    /* Make sure we have a valid entry name to work with. */
    if (full_path.name[0] == '\0')
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Open entry. */
    ni = ntfs_inode_open_from_path(vd, full_path.path);
    if (!ni) goto end;
    
    /* Open parent directory. */
    dir_ni = ntfs_inode_open_from_path(vd, full_path.dir);
    if (!dir_ni) goto end;
    
    /* Convert the entry name string from our current locale (UTF-8) into UTF-16LE. */
    uname_len = ntfs_mbstoucs(full_path.name, &uname);
    if (uname_len <= 0)
    {
        errno = EINVAL;
        goto end;
    }
    
    USBHSFS_LOG_MSG("Unlinking inode \"%s\" from \"%s\".", full_path.name, full_path.dir);
    
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
    if (!vd->update_access_times) mask &= ~NTFS_UPDATE_ATIME;
    
    /* Update entry times. */
    if (mask)
    {
        USBHSFS_LOG_MSG("Updating access times for inode %lu (mask 0x%X).", ni->mft_no, mask);
        ntfs_inode_update_times(ni, mask);
    }
}

static ntfs_inode *ntfs_inode_open_from_path_reparse(ntfs_vd *vd, const char *path, int reparse_depth)
{
    ntfs_inode *ni = NULL;
    char *target = NULL;
    
    /* Safety check. */
    if (!vd || !vd->vol || !path || !*path || reparse_depth <= 0 || reparse_depth > NTFS_MAX_SYMLINK_DEPTH)
    {
        errno = EINVAL;
        goto end;
    }
    
    USBHSFS_LOG_MSG("Opening requested inode \"%s\" (reparse depth %d).", path, reparse_depth);
    
    /* Open requested inode. */
    ni = ntfs_pathname_to_inode(vd->vol, NULL, path);
    if (!ni)
    {
        USBHSFS_LOG_MSG("Failed to open requested inode \"%s\" (errno %d).", path, errno);
        goto end;
    }
    
    USBHSFS_LOG_MSG("Successfully opened inode from path \"%s\" (mft_no %lu).", path, ni->mft_no);
    
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
        USBHSFS_LOG_MSG("Following inode symlink \"%s\" -> \"%s\".", path, target);
        ni = ntfs_inode_open_from_path_reparse(vd, target, ++reparse_depth);
        
        /* Clean up. */
        free(target);
    }
    
end:
    return ni;
}

/* This function doesn't perform checks on the provided path because it is guaranteed to be valid. */
/* Check ntfsdev_fixpath(). */
static void ntfs_split_path(const char *path, ntfs_path *p)
{
    USBHSFS_LOG_MSG("Input path: \"%s\".", path);
    
    /* Setup NTFS path. */
    memset(p, 0, sizeof(ntfs_path));
    p->path = path;
    
    /* Copy the path to internal buffer so we can modify it. */
    strcpy(p->buf, p->path);
    
    /* Split the path into separate directory and filename parts. */
    /* e.g. "/dir/file.txt" => dir: "/dir", name: "file.txt". */
    char *buf_sep = strrchr(p->buf, PATH_SEP);
    
    /* If there's just a single path separator at the start of the string, set the directory string to a path separator. */
    /* Otherwise, just use the directory string as-is. */
    p->dir = (buf_sep == p->buf ? "/" : p->buf);
    
    /* Remove the path separator we found and update the entry name pointer. */
    *buf_sep = '\0';
    p->name = (buf_sep + 1);
    
    USBHSFS_LOG_MSG("Output strings -> Path: \"%s\" | Directory: \"%s\" | Name: \"%s\".", p->path, p->dir, p->name);
}
