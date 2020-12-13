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
    formatted_str = calloc(formatted_str_len + 1, sizeof(char));
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

int ntfs_resolve_path(ntfs_vd *vd, const char *path, ntfs_path *p)
{
    const u8 *ptr = (const u8*)path;
    ssize_t units = 0;
    u32 code = 0;
    size_t len = 0;
    char *buf_sep = NULL;
    int ret = -1;
    
    /* Sanity check. */
    if (!vd || !vd->vol || !vd->root || !path || !*path || !p)
    {
        errno = EINVAL;
        goto end;
    }
    
    USBHSFS_LOG("Input path: \"%s\".", path);
    
    /* Move the path pointer to the start of the actual path. */
    do {
        units = decode_utf8(&code, ptr);
        if (units < 0)
        {
            errno = EILSEQ;
            goto end;
        }
        
        ptr += units;
    } while(code != ':' && code != 0);
    
    /* We found a colon; ptr points to the actual path. */
    if (code == ':') path = (const char*)ptr;
    
    /* Verify path length. */
    len = strlen(path);
    if (len >= USB_MAX_PATH_LENGTH)
    {
        errno = ENAMETOOLONG;
        goto end;
    }
    
    /* Setup NTFS path. */
    memset(p, 0, sizeof(ntfs_path));
    p->vol = vd->vol;
    p->path = path;
    
    /* Check if we're dealing with an empty path. */
    /* If so, just resolve to the root directory. */
    if (!len)
    {
        p->parent = vd->root;
        p->dir = NTFS_ENTRY_NAME_SELF;
        p->name = p->buf;
        ret = 0;
        goto end;
    }
    
    /* Make sure there are no more colons and that the remainder of the string is valid UTF-8. */
    ptr = (const uint8_t*)path;
    
    do {
        units = decode_utf8(&code, ptr);
        if (units < 0)
        {
            errno = EILSEQ;
            goto end;
        }
        
        if (code == ':')
        {
            errno = EINVAL;
            goto end;
        }
        
        ptr += units;
    } while(code != 0);
    
    /* Check if we're dealing with a relative path (e.g. doesn't start with a '/'). */
    /* If so, use the current directory from this volume as our parent node. */
    /* Otherwise, use the root directory node. */
    p->parent = ((p->path[0] != PATH_SEP && vd->cwd) ? vd->cwd : vd->root);
    
    /* Copy the path to internal buffer so we can modify it. */
    strcpy(p->buf, p->path);
    
    /* Check if the path ends with a trailing separator. */
    /* If so, remove it. */
    if (p->buf[len - 1] == PATH_SEP) p->buf[len - 1] = '\0';
    
    /* Check if we're dealing with a path that's exactly just "/". */
    /* If so, just resolve to the root directory. */
    if (p->buf[0] == '\0')
    {
        p->dir = NTFS_ENTRY_NAME_SELF;
        p->name = p->buf;
        ret = 0;
        goto end;
    }
    
    /* Split the path into separate directory and filename parts. */
    /* e.g. "/dir/file.txt" => dir: "/dir", name: "file.txt". */
    buf_sep = strrchr(p->buf, PATH_SEP);
    if (buf_sep)
    {
        /* There's an available path separator - split the two strings by replacing it with a NULL terminator. */
        /* If there's a single path separator at the start of the string, just set the directory string to the current directory. */
        /* If the path holds two or more separators but starts with one, make sure to skip it. */
        /* Otherwise, just use the directory string as-is. */
        *buf_sep = '\0';
        p->dir = (buf_sep == p->buf ? NTFS_ENTRY_NAME_SELF : (p->path[0] == PATH_SEP ? (p->buf + 1) : p->buf));
        p->name = (buf_sep + 1);
    } else {
        /* There is no available path separator. Treat this as a relative entry. */
        p->dir = NTFS_ENTRY_NAME_SELF;
        p->name = p->buf;
    }
    
    /* Sanity check. */
    if (strlen(p->name) > NTFS_MAX_NAME_LEN)
    {
        USBHSFS_LOG("Filename \"%s\" is too long!", p->name);
        errno = ENAMETOOLONG;
        goto end;
    }
    
    /* Update return value. */
    ret = 0;
    
end:
    if (ret == 0) USBHSFS_LOG("Output strings -> Path: \"%s\" | Directory: \"%s\" | Name: \"%s\".", path, p->path, p->dir, p->name);
    
    return ret;
}

ntfs_inode *ntfs_inode_open_from_path(ntfs_vd *vd, const char *path)
{
    return ntfs_inode_open_from_path_reparse(vd, path, 1);
}

ntfs_inode *ntfs_inode_open_from_path_reparse(ntfs_vd *vd, const char *path, int reparse_depth)
{
    const u8 *ptr = (const u8*)path;
    ssize_t units = 0;
    u32 code = 0;
    ntfs_inode *ni = NULL, *parent = NULL;
    char *target = NULL;
    
    /* Safety check. */
    if (!vd || !vd->vol || !vd->root || !path || !*path || reparse_depth < 0 || reparse_depth > NTFS_MAX_SYMLINK_DEPTH)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Move the path pointer to the start of the actual path. */
    do {
        units = decode_utf8(&code, ptr);
        if (units < 0)
        {
            errno = EILSEQ;
            goto end;
        }
        
        ptr += units;
    } while(code != ':' && code != 0);
    
    /* We found a colon; ptr points to the actual path. */
    if (code == ':') path = (const char*)ptr;
    
    /* Make sure there are no more colons and that the remainder of the string is valid UTF-8. */
    ptr = (const uint8_t*)path;
    
    do {
        units = decode_utf8(&code, ptr);
        if (units < 0)
        {
            errno = EILSEQ;
            goto end;
        }
        
        if (code == ':')
        {
            errno = EINVAL;
            goto end;
        }
        
        ptr += units;
    } while(code != 0);
    
    if (path[0] == '\0' || (path[0] == PATH_SEP && path[1] == '\0'))
    {
        /* If the path is empty or exactly '/', resolve to the root directory. */
        path = NTFS_ENTRY_NAME_SELF;
        parent = vd->root;
    } else
    if (path[0] == PATH_SEP)
    {
        /* If the path starts with '/', resolve as an absolute path from the root directory and skip the first character. */
        path++;
        parent = vd->root;
    } else {
        /* Otherwise, resolve as a relative path from current directory in the volume. */
        parent = (vd->cwd ? vd->cwd : vd->root);
    }
    
    USBHSFS_LOG("Opening requested inode \"%s\" (parent %p, reparse depth %d).", path, parent, reparse_depth);
    
    /* Open requested inode. */
    ni = ntfs_pathname_to_inode(vd->vol, parent, path);
    if (!ni)
    {
        USBHSFS_LOG("Failed to open requested inode \"%s\" (parent %p, errno %d).", path, parent, errno);
        goto end;
    }
    
    USBHSFS_LOG("Successfully opened inode from path \"%s\" (mft_no %lu).", path, ni->mft_no);
    
    /* If the entry was found and it has reparse data, then resolve the true entry. */
    /* This effectivly follows directory junctions and symbolic links until the target entry is found. */
    if ((ni->flags & FILE_ATTR_REPARSE_POINT) && ntfs_possible_symlink(ni))
    {
        /* Get the target path of this entry. */
        target = ntfs_make_symlink(ni, path);
        if (!target) goto end;
        
        /* Close this entry (we are no longer interested in it). */
        ntfs_inode_close(ni);
        
        /* Open the target entry. */
        USBHSFS_LOG("Following inode symlink \"%s\" -> \"%s\".", path, target);
        ni = ntfs_inode_open_from_path_reparse(vd, target, reparse_depth++);
        
        /* Clean up. */
        free(target);
    }
    
end:
    return ni;
}

ntfs_inode *ntfs_inode_create(ntfs_vd *vd, const char *path, mode_t type, const char *target)
{
    ntfs_path full_path = {0}, target_path = {0};
    ntfs_inode *dir_ni = NULL, *ni = NULL;
    ntfschar *uname = NULL, *utarget = NULL;
    int uname_len = 0, utarget_len = 0;
    
    /* Safety check. */
    if (!vd || !path || !*path || (type == S_IFLNK && (!target || !*target)))
    {
        errno = EINVAL;
        goto end;
    }
    
    /* TO DO: check if both paths belong to the same device. */
    /*if (vd != ntfs_volume_from_pathname(target))
    {
        errno = EXDEV;
        goto end;
    }*/
    
    /* Resolve entry path. */
    if (ntfs_resolve_path(vd, path, &full_path)) goto end;
    
    /* Check if we resolved to the root directory. */
    if (!strcmp(full_path.dir, NTFS_ENTRY_NAME_SELF) && full_path.name[0] == '\0')
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Convert the entry name string from our current locale (UTF-8) into UTF-16LE. */
    uname_len = ntfs_mbstoucs(full_path.name, &uname);
    if (uname_len <= 0)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Open the parent directory this entry will be created in. */
    dir_ni = ntfs_inode_open_from_path(vd, full_path.dir);
    if (!dir_ni) goto end;
    
    /* Create the new entry. */
    switch(type)
    {
        case S_IFDIR:   /* Directory. */
        case S_IFREG:   /* File. */
            USBHSFS_LOG("Creating inode in directory \"%s\" named \"%s\".", full_path.dir, full_path.name);
            ni = ntfs_create(dir_ni, 0, uname, uname_len, type);
            break;
        case S_IFLNK:   /* Symbolic link. */
            /* Resolve the target link path. */
            if (ntfs_resolve_path(vd, target, &target_path)) goto end;
            
            /* Check if we resolved to the root directory. */
            if (!strcmp(target_path.dir, NTFS_ENTRY_NAME_SELF) && target_path.name[0] == '\0')
            {
                errno = EINVAL;
                goto end;
            }
            
            /* Convert the target link path string from our current locale (UTF-8) into UTF-16LE. */
            utarget_len = ntfs_mbstoucs(target_path.path, &utarget);
            if (utarget_len <= 0)
            {
                errno = EINVAL;
                goto end;
            }
            
            USBHSFS_LOG("Creating symlink in directory \"%s\" named \"%s\" targetting \"%s\".", full_path.dir, full_path.name, target_path.path);
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
    ntfs_inode *dir_ni = NULL, *ni = NULL;
    ntfschar *uname = NULL;
    int ret = -1, uname_len = 0;
    
    /* Safety check. */
    if (!vd || !old_path || !*old_path || !new_path || !*new_path)
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
    
    /* Resolve entry paths. */
    if (ntfs_resolve_path(vd, old_path, &full_old_path) || ntfs_resolve_path(vd, new_path, &full_new_path)) goto end;
    
    /* Check if we resolved to the root directory. */
    if ((!strcmp(full_old_path.dir, NTFS_ENTRY_NAME_SELF) && full_old_path.name[0] == '\0') || (!strcmp(full_new_path.dir, NTFS_ENTRY_NAME_SELF) && full_new_path.name[0] == '\0'))
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Convert the entry name string from our current locale (UTF-8) into UTF-16LE. */
    uname_len = ntfs_mbstoucs(full_new_path.name, &uname);
    if (uname_len <= 0)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Open entry. */
    ni = ntfs_inode_open_from_path(vd, full_old_path.path);
    if (!ni) goto end;
    
    /* Open the new parent directory. */
    dir_ni = ntfs_inode_open_from_path(vd, full_new_path.dir);
    if (!dir_ni) goto end;
    
    /* Link the entry to its new parent directory. */
    USBHSFS_LOG("Linking inode \"%s\" to \"%s\" as \"%s\".", full_old_path.path, full_new_path.dir, full_new_path.name);
    ret = ntfs_link(ni, dir_ni, uname, uname_len);
    
end:
    if (uname) free(uname);
    
    if (ni) ntfs_inode_close(ni);
    
    if (dir_ni) ntfs_inode_close(dir_ni);
    
    return ret;
}

int ntfs_inode_unlink(ntfs_vd *vd, const char *path)
{
    ntfs_path full_path = {0};
    ntfs_inode *dir_ni = NULL, *ni = NULL;
    ntfschar *uname = NULL;
    int ret = -1, uname_len = 0;
    
    /* Resolve entry path. */
    if (ntfs_resolve_path(vd, path, &full_path)) goto end;
    
    /* Convert the entry name string from our current locale (UTF-8) into UTF-16LE. */
    uname_len = ntfs_mbstoucs(full_path.name, &uname);
    if (uname_len <= 0)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Open entry. */
    ni = ntfs_inode_open_from_path(vd, full_path.path);
    if (!ni) goto end;
    
    /* Open the entries parent directory. */
    dir_ni = ntfs_inode_open_from_path(vd, full_path.dir);
    if (!dir_ni) goto end;
    
    USBHSFS_LOG("Unlinking inode \"%s\" from \"%s\".", full_path.path, full_path.dir);
    
    /* Unlink entry from its parent. */
    /* 'ni' and 'dir_ni' are always closed by ntfs_delete(), even if it fails. */
    ret = ntfs_delete(vd->vol, full_path.path, ni, dir_ni, uname, uname_len);
    
    ni = NULL;
    dir_ni = NULL;
    
end:
    if (uname) free(uname);
    
    if (ni) ntfs_inode_close(ni);
    
    if (dir_ni) ntfs_inode_close(dir_ni);
    
    return ret;
}

int ntfs_inode_stat(ntfs_vd *vd, ntfs_inode *ni, struct stat *st)
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
    
    return 0;
}

void ntfs_inode_update_times_filtered(ntfs_vd *vd, ntfs_inode *ni, ntfs_time_update_flags mask)
{
    /* Run the access time update strategy against the volume settings first. */
    if (vd && vd->atime == ATIME_DISABLED) mask &= ~NTFS_UPDATE_ATIME;
    
    /* Update entry times. */
    if (ni && mask) ntfs_inode_update_times(ni, mask);
}
