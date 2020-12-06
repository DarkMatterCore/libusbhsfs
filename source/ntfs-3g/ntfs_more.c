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

const char *ntfs_true_pathname (const char *path)
{
    /* Sanity check. */
    if (!path)
    {
        return NULL;
    }

    /* Move the path pointer to the start of the actual path */
    /* e.g. "ums0:/dir/file.txt" => "/dir/file.txt" */
    const char *true_path = path;
    if (strchr(true_path, ':') != NULL)
    {
        true_path = strchr(true_path, ':') + 1;
    }

    /* There shouldn't be anymore colons at this point */
    if (strchr(true_path, ':') != NULL)
    {
        errno = EINVAL;
    }

    ntfs_log_trace("Resolved \"%s\" => \"%s\".", path, true_path);    
    return true_path;
}

ntfs_inode *ntfs_inode_open_pathname (ntfs_vd *vd, const char *path)
{
    return ntfs_inode_open_pathname_reparse(vd, path, 1);
}

ntfs_inode *ntfs_inode_open_pathname_reparse (ntfs_vd *vd, const char *path, int reparse_depth)
{
    ntfs_inode *ni = NULL;

    /* Get the true path of the entry. */
    path = ntfs_true_pathname(path);
    if (!path) 
    {
        errno = EINVAL;
        goto end;
    } 
    else if (path[0] == '\0') 
    {
        path = ".";
    }

    /* Find the entry within the volume. */
    if (path[0] == PATH_SEP)
    {
        /* Absolute path from volume root */
        ni = ntfs_pathname_to_inode(vd->vol, NULL, path);
    }
    else
    {
        /* Relative path from current directory */
        ni = ntfs_pathname_to_inode(vd->vol, vd->cwd, path++);
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
    ntfs_inode *dir_ni = NULL, *ni = NULL;
    char *dir = NULL;
    char *name = NULL;
    ntfschar *uname = NULL, *utarget = NULL;
    int uname_len, utarget_len;

    /* You cannot link between devices */
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

    /* Get the true paths of the entry. */
    path = ntfs_true_pathname(path);
    target = ntfs_true_pathname(target);
    if (!path)
    {
        errno = EINVAL;
        goto end;
    }

    /* Get the unicode name for the entry and find its parent directory. */
    /* TODO: This looks horrible, clean it up. */
    dir = strdup(path);
    if (!dir)
    {
        errno = EINVAL;
        goto end;
    }
    name = strrchr(dir, '/');
    if (name)
    {
        name++;
    }
    else
    {
        name = dir;
    }
    uname_len = ntfs_local_to_unicode(name, &uname);
    if (uname_len < 0)
    {
        errno = EINVAL;
        goto end;
    }
    name = strrchr(dir, '/');
    if(name)
    {
        name++;
        name[0] = 0;
    }

    /* Open the entries parent directory. */
    dir_ni = ntfs_inode_open_pathname(vd, dir);
    if (!dir_ni)
    {
        goto end;
    }

    /* Create the entry. */
    switch (type)
    {
        /* Symbolic link. */
        case S_IFLNK:
        {
            if (!target)
            {
                errno = EINVAL;
                goto end;
            }
            utarget_len = ntfs_local_to_unicode(target, &utarget);
            if (utarget_len < 0)
            {
                errno = EINVAL;
                goto end;
            }
            ni = ntfs_create_symlink(dir_ni, 0, uname, uname_len, utarget, utarget_len);
            break;
        }

        /* Directory or file. */
        case S_IFDIR:
        case S_IFREG:
        {
            ni = ntfs_create(dir_ni, 0, uname, uname_len, type);
            break;
        }

        /* Invalid entry. */
        default:
        {
            errno = EINVAL;
            goto end;
        }
    }

    /* If the entry was created. */
    if (ni)
    {
        /* Update parent directories last modify time. */
        ntfs_update_times(vd, dir_ni, NTFS_UPDATE_MCTIME);

        /* Mark the entry as dirty. */
        NInoSetDirty(ni);

        /* Mark the entry for archiving. */
        ni->flags |= FILE_ATTR_ARCHIVE;
        
        /* Sync the entry (and attributes). */
        ntfs_inode_sync(ni);
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
        ntfs_inode_sync(dir_ni);
        ntfs_inode_close(dir_ni);
    }

    if (dir)
    {
        free(dir);
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
