/*
 * ntfs.c
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#include "ntfs.h"

/* Function prototypes. */

static ntfs_inode *ntfs_inode_open_from_path_reparse(ntfs_vd *vd, const char *path, int reparse_depth);

#ifdef DEBUG
int ntfs_log_handler_usbhsfs(const char *function, const char *file, int line, u32 level, void *data, const char *format, va_list args)
{
	NX_IGNORE_ARG(data);

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
        usbHsFsLogWriteFormattedStringToLogFile(file, line, function, "%s (level %d).", formatted_str, level);
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

ntfs_inode *ntfs_inode_create(ntfs_vd *vd, const ntfs_path *path, mode_t type, const char *target)
{
    ntfs_inode *dir_ni = NULL, *ni = NULL;
    ntfschar *uname = NULL, *utarget = NULL;
    int uname_len = 0, utarget_len = 0;

    /* Safety check. */
    if (!vd || !vd->vol || !path || !path->dir || !path->name || (type == S_IFLNK && (!target || !*target)))
    {
        errno = EINVAL;
        goto end;
    }

    /* Open the parent directory the desired entry will be created in. */
    dir_ni = ntfs_inode_open_from_path(vd, path->dir);
    if (!dir_ni) goto end;

    /* Convert the entry name string from our current locale (UTF-8) into UTF-16LE. */
    uname_len = ntfs_mbstoucs(path->name, &uname);
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
            USBHSFS_LOG_MSG("Creating inode \"%s\" in directory \"%s\".", path->name, path->dir);
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

            USBHSFS_LOG_MSG("Creating symlink in directory \"%s\" named \"%s\" targetting \"%s\".", path->dir, path->name, target);
            ni = ntfs_create_symlink(dir_ni, 0, uname, uname_len, utarget, utarget_len);
            break;
        default:        /* Invalid entry. */
            errno = EINVAL;
            break;
    }

    if (!ni) USBHSFS_LOG_MSG("NTFS inode creation failed for \"%s\" (%d).", path->path, errno);

end:
    if (utarget) free(utarget);

    if (uname) free(uname);

    if (dir_ni) ntfs_inode_close(dir_ni);

    return ni;
}

int ntfs_inode_link(ntfs_vd *vd, const ntfs_path *old_path, const ntfs_path *new_path)
{
    ntfs_inode *ni = NULL, *dir_ni = NULL;
    ntfschar *uname = NULL;
    int ret = -1, uname_len = 0;

    /* Safety check. */
    if (!vd || !vd->vol || !old_path || !old_path->path || !new_path || !new_path->dir || !new_path->name)
    {
        errno = EINVAL;
        goto end;
    }

    /* Open the entry we will create a symlink for. */
    ni = ntfs_inode_open_from_path(vd, old_path->path);
    if (!ni) goto end;

    /* Open new parent directory. */
    dir_ni = ntfs_inode_open_from_path(vd, new_path->dir);
    if (!dir_ni) goto end;

    /* Convert the entry name string from our current locale (UTF-8) into UTF-16LE. */
    uname_len = ntfs_mbstoucs(new_path->name, &uname);
    if (uname_len <= 0)
    {
        errno = EINVAL;
        goto end;
    }

    USBHSFS_LOG_MSG("Linking inode \"%s\" to \"%s\".", old_path->path, new_path->path);

    /* Link the entry to its new parent directory. */
    ret = ntfs_link(ni, dir_ni, uname, uname_len);
    if (ret) USBHSFS_LOG_MSG("Failed to create NTFS inode link for \"%s\" (ret %d, errno %d).", new_path->path, ret, errno);

end:
    if (uname) free(uname);

    if (dir_ni) ntfs_inode_close(dir_ni);

    if (ni) ntfs_inode_close(ni);

    return ret;
}

int ntfs_inode_unlink(ntfs_vd *vd, const ntfs_path *path)
{
    ntfs_inode *ni = NULL, *dir_ni = NULL;
    ntfschar *uname = NULL;
    int ret = -1, uname_len = 0;

    /* Safety check. */
    if (!vd || !vd->vol || !path || !path->path || !path->dir || !path->name)
    {
        errno = EINVAL;
        goto end;
    }

    /* Open entry. */
    ni = ntfs_inode_open_from_path(vd, path->path);
    if (!ni) goto end;

    /* Open parent directory. */
    dir_ni = ntfs_inode_open_from_path(vd, path->dir);
    if (!dir_ni) goto end;

    /* Convert the entry name string from our current locale (UTF-8) into UTF-16LE. */
    uname_len = ntfs_mbstoucs(path->name, &uname);
    if (uname_len <= 0)
    {
        errno = EINVAL;
        goto end;
    }

    USBHSFS_LOG_MSG("Unlinking inode \"%s\" from \"%s\".", path->name, path->dir);

    /* Unlink entry from its parent. */
    ret = ntfs_delete(vd->vol, path->path, ni, dir_ni, uname, uname_len);
    if (ret) USBHSFS_LOG_MSG("Failed to unlink NTFS inode \"%s\" (ret %d, errno %d).", path->path, ret, errno);

    /* 'ni' and 'dir_ni' are always closed by ntfs_delete(), even if it fails. */
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
        char *target = ntfs_make_symlink(ni, path);
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
