/**
 * ea.c - Processing of EA's
 *
 *      This module is part of ntfs-3g library
 *
 * Copyright (c) 2014 Jean-Pierre Andre
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the NTFS-3G
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "types.h"
#include "param.h"
#include "layout.h"
#include "attrib.h"
#include "index.h"
#include "dir.h"
#include "ea.h"
#include "misc.h"
#include "logging.h"
#include "xattrs.h"

/*
 *		Create a needed attribute (EA or EA_INFORMATION)
 *
 *	Returns 0 if successful,
 *		-1 otherwise, with errno indicating why it failed.
 */

static int ntfs_need_ea(ntfs_inode *ni, ATTR_TYPES type, int size, int flags)
{
	u8 dummy;
	int res;

	res = 0;
	if (!ntfs_attr_exist(ni,type, AT_UNNAMED,0)) {
		if (!(flags & XATTR_REPLACE)) {
			/*
			 * no needed attribute : add one,
			 * apparently, this does not feed the new value in
			 * Note : NTFS version must be >= 3
			 */
			if (ni->vol->major_ver >= 3) {
				res = ntfs_attr_add(ni,	type,
					AT_UNNAMED,0,&dummy,(s64)size);
				if (!res) {
					    NInoFileNameSetDirty(ni);
				}
				NInoSetDirty(ni);
			} else {
				errno = EOPNOTSUPP;
				res = -1;
			}
		} else {
			errno = ENODATA;
			res = -1;
		}
	}
	return (res);
}

/*
 *		Restore the old EA_INFORMATION or delete the current one,
 *	 when EA cannot be updated.
 *
 *	As this is used in the context of some other error, the caller
 *	is responsible for returning the proper error, and errno is
 *	left unchanged.
 *	Only double errors are logged here.
 */

static void restore_ea_info(ntfs_attr *nai, const EA_INFORMATION *old_ea_info)
{
	s64 written;
	int olderrno;

	olderrno = errno;
	if (old_ea_info) {
		written = ntfs_attr_pwrite(nai,	0, sizeof(EA_INFORMATION),
				old_ea_info);
		if ((size_t)written != sizeof(EA_INFORMATION)) {
			ntfs_log_error("Could not restore the EA_INFORMATION,"
				" possible inconsistency in inode %lld\n",
				(long long)nai->ni->mft_no);
		}
	} else {
		if (ntfs_attr_rm(nai)) {
			ntfs_log_error("Could not delete the EA_INFORMATION,"
				" possible inconsistency in inode %lld\n",
				(long long)nai->ni->mft_no);
		}
	}
	errno = olderrno;
}

/*
 *		Update both EA and EA_INFORMATION
 */

static int ntfs_update_ea(ntfs_inode *ni, const char *value, size_t size,
			const EA_INFORMATION *ea_info,
			const EA_INFORMATION *old_ea_info)
{
	ntfs_attr *na;
	ntfs_attr *nai;
	int res;

	res = 0;
	nai = ntfs_attr_open(ni, AT_EA_INFORMATION, AT_UNNAMED, 0);
	if (nai) {
		na = ntfs_attr_open(ni, AT_EA, AT_UNNAMED, 0);
		if (na) {
				/*
				 * Set EA_INFORMATION first, it is easier to
				 * restore the old value, if setting EA fails.
				 */
			if (ntfs_attr_pwrite(nai, 0, sizeof(EA_INFORMATION),
						ea_info)
					!= (s64)sizeof(EA_INFORMATION)) {
				res = -errno;
			} else {
				if (((na->data_size > (s64)size)
					&& ntfs_attr_truncate(na, size))
				    || (ntfs_attr_pwrite(na, 0, size, value)
							!= (s64)size)) {
					res = -errno;
                                        if (old_ea_info)
						restore_ea_info(nai,
							old_ea_info);
				}
			}
			ntfs_attr_close(na);
		}
		ntfs_attr_close(nai);
	} else {
		res = -errno;
	}
	return (res);
}

/*
 *		Return the existing EA
 *
 *	The EA_INFORMATION is not examined and the consistency of the
 *	existing EA is not checked.
 *
 *	If successful, the full attribute is returned unchanged
 *		and its size is returned.
 *	If the designated buffer is too small, the needed size is
 *		returned, and the buffer is left unchanged.
 *	If there is an error, a negative value is returned and errno
 *		is set according to the error.
 */

int ntfs_get_ntfs_ea(ntfs_inode *ni, char *value, size_t size)
{
	s64 ea_size;
	void *ea_buf;
	int res = 0;

	if (ntfs_attr_exist(ni, AT_EA, AT_UNNAMED, 0)) {
		ea_buf = ntfs_attr_readall(ni, AT_EA, (ntfschar*)NULL, 0,
					&ea_size);
		if (ea_buf) {
			if (value && (ea_size <= (s64)size))
				memcpy(value, ea_buf, ea_size);
			free(ea_buf);
			res = ea_size;
		} else {
			ntfs_log_error("Failed to read EA from inode %lld\n",
					(long long)ni->mft_no);
			errno = ENODATA;
			res = -errno;
		}
	} else {
		errno = ENODATA;
		res = -errno;
	}
	return (res);
}

/*
 *		Set a new EA, and set EA_INFORMATION accordingly
 *
 *	This is roughly the same as ZwSetEaFile() on Windows, however
 *	the "offset to next" of the last EA should not be cleared.
 *
 *	Consistency of the new EA is first checked.
 *
 *	EA_INFORMATION is set first, and it is restored to its former
 *	state if setting EA fails.
 *
 *	Returns 0 if successful
 *		a negative value if an error occurred.
 */

int ntfs_set_ntfs_ea(ntfs_inode *ni, const char *value, size_t size, int flags)
{
	EA_INFORMATION ea_info;
	EA_INFORMATION *old_ea_info;
	s64 old_ea_size;
	int res;
	size_t offs;
	size_t nextoffs;
	BOOL ok;
	int ea_count;
	int ea_packed;
	const EA_ATTR *p_ea;

	res = -1;
	if (value && (size > 0)) {
					/* do consistency checks */
		offs = 0;
		ok = TRUE;
		ea_count = 0;
		ea_packed = 0;
		nextoffs = 0;
		while (ok && (offs < size)) {
			p_ea = (const EA_ATTR*)&value[offs];
			nextoffs = offs + le32_to_cpu(p_ea->next_entry_offset);
				/* null offset to next not allowed */
			ok = (nextoffs > offs)
			    && (nextoffs <= size)
			    && !(nextoffs & 3)
			    && p_ea->name_length
				/* zero sized value are allowed */
			    && ((offs + offsetof(EA_ATTR,name)
				+ p_ea->name_length + 1
				+ le16_to_cpu(p_ea->value_length))
				    <= nextoffs)
			    && ((offs + offsetof(EA_ATTR,name)
				+ p_ea->name_length + 1
				+ le16_to_cpu(p_ea->value_length))
				    >= (nextoffs - 3))
			    && !p_ea->name[p_ea->name_length];
			/* name not checked, as chkdsk accepts any chars */
			if (ok) {
				if (p_ea->flags & NEED_EA)
					ea_count++;
				/*
				 * Assume ea_packed includes :
				 * 4 bytes for header (flags and lengths)
				 * + name length + 1
				 * + value length
				 */
				ea_packed += 5 + p_ea->name_length
					+ le16_to_cpu(p_ea->value_length);
				offs = nextoffs;
			}
		}
		/*
		 * EA and REPARSE_POINT exclude each other
		 * see http://msdn.microsoft.com/en-us/library/windows/desktop/aa364404(v=vs.85).aspx
		 * Also return EINVAL if REPARSE_POINT is present.
		 */
		if (ok
		    && !ntfs_attr_exist(ni, AT_REPARSE_POINT, AT_UNNAMED,0)) {
			ea_info.ea_length = cpu_to_le16(ea_packed);
			ea_info.need_ea_count = cpu_to_le16(ea_count);
			ea_info.ea_query_length = cpu_to_le32(nextoffs);

			old_ea_size = 0;
			old_ea_info = NULL;
				/* Try to save the old EA_INFORMATION */
			if (ntfs_attr_exist(ni, AT_EA_INFORMATION,
							AT_UNNAMED, 0)) {
				old_ea_info = ntfs_attr_readall(ni,
					AT_EA_INFORMATION,
					(ntfschar*)NULL, 0, &old_ea_size);
			}
			/*
			 * no EA or EA_INFORMATION : add them
			 */
			if (!ntfs_need_ea(ni, AT_EA_INFORMATION,
					sizeof(EA_INFORMATION), flags)
			    && !ntfs_need_ea(ni, AT_EA, 0, flags)) {
				res = ntfs_update_ea(ni, value, size,
						&ea_info, old_ea_info);
			} else {
				res = -errno;
			}
			if (old_ea_info)
				free(old_ea_info);
		} else {
			errno = EINVAL;
			res = -errno;
		}
	} else {
		errno = EINVAL;
		res = -errno;
	}
	return (res);
}

/*
 *		Remove the EA (including EA_INFORMATION)
 *
 *	EA_INFORMATION is removed first, and it is restored to its former
 *	state if removing EA fails.
 *
 *	Returns 0, or -1 if there is a problem
 */

int ntfs_remove_ntfs_ea(ntfs_inode *ni)
{
	EA_INFORMATION *old_ea_info;
	s64 old_ea_size;
	int res;
	ntfs_attr *na;
	ntfs_attr *nai;

	res = 0;
	if (ni) {
		/*
		 * open and delete the EA_INFORMATION and the EA
		 */
		nai = ntfs_attr_open(ni, AT_EA_INFORMATION, AT_UNNAMED, 0);
		if (nai) {
			na = ntfs_attr_open(ni, AT_EA, AT_UNNAMED, 0);
			if (na) {
				/* Try to save the old EA_INFORMATION */
				old_ea_info = ntfs_attr_readall(ni,
					 AT_EA_INFORMATION,
					 (ntfschar*)NULL, 0, &old_ea_size);
				res = ntfs_attr_rm(na);
				NInoFileNameSetDirty(ni);
				if (!res) {
					res = ntfs_attr_rm(nai);
					if (res && old_ea_info) {
					/*
					 * Failed to remove the EA, try to
					 * restore the EA_INFORMATION
					 */
						restore_ea_info(nai,
							old_ea_info);
					}
				} else {
					ntfs_log_error("Failed to remove the"
						" EA_INFORMATION from inode %lld\n",
						(long long)ni->mft_no);
				}
				free(old_ea_info);
				ntfs_attr_close(na);
			} else {
				/* EA_INFORMATION present, but no EA */
				res = ntfs_attr_rm(nai);
				NInoFileNameSetDirty(ni);
			}
			ntfs_attr_close(nai);
		} else {
			errno = ENODATA;
			res = -1;
		}
		NInoSetDirty(ni);
	} else {
		errno = EINVAL;
		res = -1;
	}
	return (res ? -1 : 0);
}
