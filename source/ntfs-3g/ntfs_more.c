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

ntfs_path ntfs_resolve_path (ntfs_vd *vd, const char *path)
{
    ntfs_path ret;
    ret.buf[0] = '\0';
    ret.vol = vd->vol;
    ret.parent = NULL;
    ret.path = path;
    ret.dir = NULL;
    ret.name = NULL;

    /* Sanity check. */
    if (!ret.vol || !ret.path)
    {
        errno = EINVAL;
        return ret;
    }

    /* Sanity check. */
    if (strlen(ret.path) > FS_MAX_PATH)
    {
        errno = ERANGE;
        return ret;
    }

    /* Remove the mount prefix (e.g. "ums0:/dir/file.txt" => "/dir/file.txt"). */
    if (strchr(ret.path, ':') != NULL)
    {
        ret.path = strchr(ret.path, ':') + 1;
    }

    /* Is this a relative path? (i.e. doesn't start with a '/') */
    if (ret.path[0] != PATH_SEP)
    {
        /* Use the volumes current directory as our parent node */
        ret.parent = vd->cwd;
    }
  
    /* Copy the path to internal buffer that we can modify it. */
    strcpy(ret.buf, ret.path);
    if (!ret.buf)
    {
        errno = ENOMEM;
        return ret;
    }

    /* Split the path in to seperate directory and file name parts. */
    /* e.g. "/dir/file.txt" => dir: "/dir", name: "file.txt" */
    char *path_sep = strrchr(ret.path, PATH_SEP);
    if (path_sep)
    {
        /* There is a path seperator present, split the two values */
        *(path_sep) = '\0'; /* Null terminate the string to seperate the 'dir' and 'name' components. */
        ret.dir = ret.path; /* The directory is the first part of the path. */
        ret.name = (path_sep + 1); /* The name is the second part of the path.  */
    }
    else
    {
        /* There is no path seperator present, only a file name */
        ret.dir = "."; /* Use the current directory alias */
        ret.name = ret.path; /* The name is the entire 'path' */
    }

    return ret;
}

ntfs_inode *ntfs_inode_open_pathname (ntfs_vd *vd, const char *path)
{
    return ntfs_inode_open_pathname_reparse(vd, path, 1);
}

ntfs_inode *ntfs_inode_open_pathname_reparse (ntfs_vd *vd, const char *path, int reparse_depth)
{
    ntfs_inode *ni = NULL;

    /* Resolve the path name of the entry. */
    if (strchr(path, ':') != NULL) {
        path = strchr(path, ':') + 1;
    }
    if (strchr(path, ':') != NULL) {
        path = ".";
    }
    if (path[0] == '\0') {
        path = ".";
    }
    if (path[0] == PATH_SEP && path[1] == '\0')
    {
        ni = ntfs_pathname_to_inode(vd->vol, NULL, ".");
        ntfs_log_trace("OPEN %p \"%s\"", ni, ".");
    }
    else if (path[0] != PATH_SEP)
    {
        ni = ntfs_pathname_to_inode(vd->vol, vd->cwd, path++);
        ntfs_log_trace("OPEN %p \"%s\"", ni, path);
    }
    else
    {
        ni = ntfs_pathname_to_inode(vd->vol, NULL, path);
        ntfs_log_trace("OPEN %p \"%s\"", ni, path);
    }

    if (!ni) 
    {
        goto end;
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
            ntfs_log_trace("following symlink for inode at \"%s\" => \"%s\"", path, target);
            ni = ntfs_inode_open_pathname_reparse(vd, target, reparse_depth++);

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

    /* You cannot link entries across devices */
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
    full_path = ntfs_resolve_path(vd, path);
    if (!full_path.dir || !full_path.name)
    {
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
    dir_ni = ntfs_inode_open_pathname(vd, full_path.dir);
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
            ni = ntfs_create(dir_ni, 0, uname, uname_len, type);
            break;
        }

        /* Symbolic link. */
        case S_IFLNK:
        {
            /* Resolve the link target path */
            target_path = ntfs_resolve_path(vd, path);
            if (!target_path.dir || !target_path.name)
            {
                goto end;
            }
            
            /* Convert the link target path to unicode. */
            utarget_len = ntfs_local_to_unicode(target_path.path, &utarget);
            if (utarget_len < 0)
            {
                errno = EINVAL;
                goto end;
            }

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

    /* If the new entry was created. */
    if (ni && dir_ni)
    {
        /* Update the entries last modify time. */
        ntfs_update_times(vd, ni, NTFS_UPDATE_MCTIME);
        ntfs_update_times(vd, dir_ni, NTFS_UPDATE_MCTIME);

        /* Mark the entries as dirty. */
        NInoSetDirty(ni);
        NInoSetDirty(dir_ni);

        /* Mark the entries for archiving. */
        ni->flags |= FILE_ATTR_ARCHIVE;
        dir_ni->flags |= FILE_ATTR_ARCHIVE;
        
        /* Sync the entries (and attributes). */
        ntfs_inode_sync(ni);
        ntfs_inode_sync(dir_ni);
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

int ntfs_stat (ntfs_vd *vd, ntfs_inode *ni, struct stat *st)
{
    int ret = 0;

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

    return ret;
}

void ntfs_update_times (ntfs_vd *vd, ntfs_inode *ni, ntfs_time_update_flags mask)
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
