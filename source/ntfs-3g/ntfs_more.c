/*
 * ntfs_more.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 * This file is based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#include <sys/iosupport.h>
#include <sys/reent.h>
#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>

#include "ntfs.h"
#include "ntfs_more.h"

#include <ntfs-3g/dir.h>
#include <ntfs-3g/reparse.h>

int ntfs_resolve_path (ntfs_vd *vd, const char *path, ntfs_path *p)
{
    p->buf[0] = '\0';
    p->vol = vd->vol;
    p->parent = NULL;
    p->path = path;
    p->dir = NULL;
    p->name = NULL;

    /* Sanity check. */
    if (!p->vol || !p->path)
    {
        errno = EINVAL;
        return -1;
    }

    /* Sanity check. */
    if (strlen(p->path) > FS_MAX_PATH)
    {
        ntfs_log_error("path \"%s\" is too long", p->path);
        errno = ERANGE;
        return -1;
    }

    /* Remove the mount prefix (e.g. "ums0:/dir/file.txt" => "/dir/file.txt"). */
    if (strchr(p->path, ':') != NULL)
    {
        p->path = strchr(p->path, ':') + 1;
    }

    /* Is this a relative path from the current directory? (i.e. doesn't start with a '/') */
    if (p->path[0] != PATH_SEP && vd->cwd)
    {
        /* Use the volumes current directory as our parent node. */
        p->parent = vd->cwd;
    }
    else
    {
        /* Use the volumes top-most directory (root) as our parent node. */
        p->parent = vd->root;
    }
  
    /* Copy the path to internal buffer that we can modify it. */
    strcpy(p->buf, p->path);
    if (!p->buf)
    {
        errno = ENOMEM;
        return -1;
    }

    /* Split the path in to seperate directory and file name parts. */
    /* e.g. "/dir/file.txt" => dir: "/dir", name: "file.txt" */
    char *buf_sep = strrchr(p->buf, PATH_SEP);
    if (buf_sep)
    {
        /* There is a path seperator present, split the two values */
        *(buf_sep) = '\0'; /* Null terminate the string to seperate the 'dir' and 'name' components. */
        p->dir = p->buf; /* The directory is the first part of the path. */
        p->name = (buf_sep + 1); /* The name is the second part of the path.  */
    }
    else
    {
        /* There is no path seperator present, only a file name */
        p->dir = NTFS_ENTRY_NAME_SELF; /* Use the self directory reference */
        p->name = p->buf; /* The name is the entire 'path' */
    }

    /* Sanity check. */
    if (p->name && strlen(p->name) > NTFS_MAX_NAME_LEN)
    {
        ntfs_log_error("file name \"%s\" is too long", p->name);
        errno = ERANGE;
        return -1;
    }
    
    ntfs_log_debug("\"%s\" -> path: \"%s\", dir: \"%s\", name: \"%s\"", path, p->path, p->dir, p->name);
    return 0;
}

ntfs_inode *ntfs_inode_open_from_path (ntfs_vd *vd, const char *path)
{
    return ntfs_inode_open_from_path_reparse(vd, path, 1);
}

ntfs_inode *ntfs_inode_open_from_path_reparse (ntfs_vd *vd, const char *path, int reparse_depth)
{
    ntfs_inode *ni = NULL;
    ntfs_inode *parent = NULL;

    /* Remove the mount prefix (e.g. "ums0:/dir/file.txt" => "/dir/file.txt"). */
    if (strchr(path, ':') != NULL) 
    {
        path = strchr(path, ':') + 1;
    }

    /* If the path is empty or exactly '/', resolve to the top-most directory (root). */
    if ((path[0] == '\0') ||
        (path[0] == PATH_SEP && path[1] == '\0'))
    {
        path = NTFS_ENTRY_NAME_SELF;
    }

    /* If the path starts with '/', resolve as an absolute path from the top-most directory (root). */
    else if (path[0] == PATH_SEP)
    {
        path++; /* Skip over the first '/' character. */
    }

    /* Otherwise, resolve as a relative path from the volumes current directory (cwd). */
    else
    {
        parent = vd->cwd;
    }

    /* Resolve the path name of the entry. */
    ntfs_log_debug("opening inode from path \"%s\" (parent %p)", path, parent);
    ni = ntfs_pathname_to_inode(vd->vol, parent, path);
    if (!ni) 
    {
        ntfs_log_debug("failed to open inode from path \"%s\" (errno %i)", path, errno);
        goto end;
    }
    else 
    {
        ntfs_log_debug("successfully opened inode from path \"%s\" (mft_no %lu)", path, ni->mft_no);
    }

    /* If the entry was found and it has reparse data then resolve the true entry. */
    /* This effectivly follows directory junctions and symbolic links until the target entry is found. */
    if (ni && (ni->flags & FILE_ATTR_REPARSE_POINT))
    {
        /* Is this is entry a junction point or symbolic link? */
        if (ntfs_possible_symlink(ni))
        {
            /* Sanity check, give up if we are parsing to deep. */
            if (reparse_depth > NTFS_MAX_SYMLINK_DEPTH)
            {
                ntfs_log_error("inode symlink depth exceeded, giving up");
                ntfs_inode_close(ni);
                ni = NULL;
                errno = ELOOP;
                goto end;
            }

            /* Get the target path of this entry. */
            char *target = ntfs_make_symlink(ni, path);
            if (!target)
            {
                goto end;
            }

            /* Close this entry (we are no longer interested in it). */
            ntfs_inode_close(ni);

            /* Open the target entry. */
            ntfs_log_debug("following inode symlink \"%s\" -> \"%s\"", path, target);
            ni = ntfs_inode_open_from_path_reparse(vd, target, reparse_depth++);

            /* Clean up. */
            free(target);
        }
    }

end:

    return ni;
}

ntfs_inode *ntfs_inode_create (ntfs_vd *vd, const char *path, mode_t type, const char *target)
{
    ntfs_path full_path, target_path;
    ntfs_inode *dir_ni = NULL, *ni = NULL;
    ntfschar *uname = NULL, *utarget = NULL;
    int uname_len, utarget_len;

    /* You cannot link entries across devices. */
    if (path && target)
    {
        /* TODO: Check that both paths belong to the same device.
        if (vd != ntfs_volume_from_pathname(target))
        {
            errno = EXDEV;
            goto end;
        }
        */
    }

    /* Resolve the entry path */
    if (ntfs_resolve_path(vd, path, &full_path) || !full_path.dir || !full_path.name)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Convert the entry name to unicode. */
    uname_len = ntfs_local_to_unicode(full_path.name, &uname);
    if (uname_len < 0)
    {
        errno = EINVAL;
        goto end;
    }

    /* Open the parent directory this entry will be created in. */
    dir_ni = ntfs_inode_open_from_path(vd, full_path.dir);
    if (!dir_ni)
    {
        goto end;
    }

    /* Create the new entry. */
    switch (type)
    {
        /* Directory or file. */
        case S_IFDIR:
        case S_IFREG:
        {
            ntfs_log_debug("creating inode in directory \"%s\" named \"%s\"", full_path.dir, full_path.name);
            ni = ntfs_create(dir_ni, 0, uname, uname_len, type);
            break;
        }

        /* Symbolic link. */
        case S_IFLNK:
        {
            /* Resolve the link target path */
            if (ntfs_resolve_path(vd, target, &target_path) || !target_path.path)
            {
                errno = EINVAL;
                goto end;
            }
            
            /* Convert the link target path to unicode. */
            utarget_len = ntfs_local_to_unicode(target_path.path, &utarget);
            if (utarget_len < 0)
            {
                errno = EINVAL;
                goto end;
            }

            ntfs_log_debug("creating symlink in directory \"%s\" named \"%s\" targetting \"%s\"", full_path.dir, full_path.name, target_path.path);
            ni = ntfs_create_symlink(dir_ni, 0, uname, uname_len, utarget, utarget_len);
            break;
        }

        /* Invalid entry. */
        default:
        {
            errno = EINVAL;
            goto end;
        }
    }

end:

    if (utarget)
    {
        free(utarget);
    }

    if (uname)
    {
        free(uname);
    }

    if (dir_ni)
    {
        ntfs_inode_close(dir_ni);
    }

    return ni;
}

int ntfs_inode_link (ntfs_vd *vd, const char *old_path, const char *new_path)
{
    ntfs_path full_old_path, full_new_path;
    ntfs_inode *dir_ni = NULL, *ni = NULL;
    ntfschar *uname = NULL;
    int uname_len;
    int ret = -1;

    /* You cannot link entries across devices. */
    if (old_path && new_path)
    {
        /* TODO: Check that both paths belong to the same device.
        if (vd != ntfs_volume_from_pathname(new_path))
        {
            errno = EXDEV;
            goto end;
        }
        */
    }
    
    /* Resolve the entry paths */
    if (ntfs_resolve_path(vd, old_path, &full_old_path) || !full_old_path.path ||
        ntfs_resolve_path(vd, new_path, &full_new_path) || !full_new_path.dir || !full_new_path.name)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Convert the entry name to unicode. */
    uname_len = ntfs_local_to_unicode(full_new_path.name, &uname);
    if (uname_len < 0)
    {
        errno = EINVAL;
        goto end;
    }

    /* Open the entry. */
    ni = ntfs_inode_open_from_path(vd, full_old_path.path);
    if (!ni)
    {
        goto end;
    }

    /* Open the entries new parent directory. */
    dir_ni = ntfs_inode_open_from_path(vd, full_new_path.dir);
    if (!dir_ni)
    {
        goto end;
    }

    /* Link the entry to its new parent directory. */
    ntfs_log_debug("linking inode \"%s\" to \"%s\" as \"%s\"", full_old_path.path, full_new_path.dir, full_new_path.name);
    ret = ntfs_link(ni, dir_ni, uname, uname_len);

end:

    if (uname)
    {
        free(uname);
    }

    if (ni)
    {
        ntfs_inode_close(ni);
    }

    if (dir_ni)
    {
        ntfs_inode_close(dir_ni);
    }

    return ret;
}

int ntfs_inode_unlink (ntfs_vd *vd, const char *path)
{
    ntfs_path full_path;
    ntfs_inode *dir_ni = NULL, *ni = NULL;
    ntfschar *uname = NULL;
    int uname_len;
    int ret = -1;

    /* Resolve the entry path */
    if (ntfs_resolve_path(vd, path, &full_path) || !full_path.path || !full_path.dir || !full_path.name)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Convert the entry name to unicode. */
    uname_len = ntfs_local_to_unicode(full_path.name, &uname);
    if (uname_len < 0)
    {
        errno = EINVAL;
        goto end;
    }

    /* Open the entry. */
    ni = ntfs_inode_open_from_path(vd, full_path.path);
    if (!ni)
    {
        goto end;
    }

    /* Open the entries parent directory. */
    dir_ni = ntfs_inode_open_from_path(vd, full_path.dir);
    if (!dir_ni)
    {
        goto end;
    }

    /* Unlink the entry from its parent. */
    /* NOTE: 'ni' and 'dir_ni' are always closed after the call to this function (even if it failed) */
    ntfs_log_debug("unlinking inode \"%s\" from \"%s\"", full_path.path, full_path.dir);
    ret = ntfs_delete(vd->vol, full_path.path, ni, dir_ni, uname, uname_len);
    ni = NULL;
    dir_ni = NULL;
    
end:

    if (uname)
    {
        free(uname);
    }

    if (ni)
    {
        ntfs_inode_close(ni);
    }

    if (dir_ni)
    {
        ntfs_inode_close(dir_ni);
    }

    return ret;
}

int ntfs_inode_stat (ntfs_vd *vd, ntfs_inode *ni, struct stat *st)
{
    /* Zero out the stat buffer. */
    memset(st, 0, sizeof(struct stat));

    /* Fill in the generic stats. */
    st->st_dev = vd->id;
    st->st_ino = ni->mft_no;
    st->st_uid = vd->uid;
    st->st_gid = vd->gid;
    st->st_atime = ni->last_access_time;
    st->st_ctime = ni->last_mft_change_time;
    st->st_mtime = ni->last_data_change_time;

    /* Is this a directory? */
    if (ni->mrec->flags & MFT_RECORD_IS_DIRECTORY)
    {
        st->st_mode = S_IFDIR | (0777 & ~vd->dmask);
        st->st_nlink = 1;

        /* Open the directory index allocation table attribute to get size stats */
        ntfs_attr *na = ntfs_attr_open(ni, AT_INDEX_ALLOCATION, NTFS_INDEX_I30, 4);
        if (na) {
            st->st_size = na->data_size;
            st->st_blocks = na->allocated_size >> 9;
            ntfs_attr_close(na);
            na = NULL;
        }
    }

    /* Else it must be a file. */
    else 
    {
        st->st_mode = S_IFREG | (0777 & ~vd->fmask);
        st->st_nlink = le16_to_cpu(ni->mrec->link_count);
        st->st_size = ni->data_size;
        st->st_blocks = (ni->allocated_size + 511) >> 9;
    }

    return 0;
}

void ntfs_inode_update_times_filtered (ntfs_vd *vd, ntfs_inode *ni, ntfs_time_update_flags mask)
{
    /* Run the access time update strategy against the volume settings first. */
    if (vd && vd->atime == ATIME_DISABLED)
    {
        mask &= ~NTFS_UPDATE_ATIME;
    }

    /* Update entry times. */
    if (ni && mask)
    {
        ntfs_inode_update_times(ni, mask);
    }
}

int ntfs_unicode_to_local (const ntfschar *ins, const int ins_len, char **outs, int outs_len)
{
    int len = 0;

    /* Sanity check. */
    if (!ins || !ins_len || !outs)
    {
        return 0;
    }

    /* Convert the unicode string to current local. */
    len = ntfs_ucstombs(ins, ins_len, outs, outs_len);

    /* If the string could not be converted automatically. */ 
    if (len == -1 && errno == EILSEQ)
    {
        /* Convert manually by replacing non-ASCII characters with underscores. */
        if (!*outs || outs_len >= ins_len)
        {
            if (!*outs)
            {
                *outs = (char *) calloc(1, ins_len + 1);
                if (!*outs)
                {
                    errno = ENOMEM;
                    return -1;
                }
            }
            int i;
            for (i = 0; i < ins_len; i++)
            {
                ntfschar uc = le16_to_cpu(ins[i]);
                if (uc > 0xff)
                {
                    uc = (ntfschar) '_';
                }
                *outs[i] = (char) uc;
            }
            *outs[ins_len] = (ntfschar)'\0';
            len = ins_len;
        }
    }

    return len;
}

int ntfs_local_to_unicode (const char *ins, ntfschar **outs)
{
    /* Sanity check. */
    if (!ins || !outs)
    {
        return 0;
    }

    /* Convert the local string to unicode. */
    return ntfs_mbstoucs(ins, outs);
}
