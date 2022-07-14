/*
 * usbfs.h
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, Blake Warner.
 * Copyright (c) 2018, Team Xecuter.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#ifndef __USBFS_H__
#define __USBFS_H__

#include <switch.h>

#define USBFS_MOUNT_NAME        "usbhdd"

#define USBFS_UNMOUNTED         0
#define USBFS_MOUNTED           1
#define USBFS_UNSUPPORTED_FS    2

Result usbFsInitialize(void);
void usbFsExit(void);

Result usbFsGetMountStatus(u64 *status);
Result usbFsOpenFile(u64 *fileid, const char *filepath, u64 mode);
Result usbFsCloseFile(u64 fileid);
Result usbFsReadFile(u64 fileid, void *buffer, size_t size, size_t *retsize);
Result usbFsIsReady();
Result usbFsOpenDir(u64 *dirid, const char *dirpath);
Result usbFsCloseDir(u64 dirid);
Result usbFsReadDir(u64 dirid, u64 *type, u64 *size, char *name, size_t namemax);
Result usbFsCreateDir(const char *dirpath);
Result usbFsSeekFile(u64 fileid, u64 pos, u64 whence, u64 *retpos);
Result usbFsReadRaw(u64 sector, u64 sectorcount, void *buffer);
Result usbFsWriteFile(u64 fileid, const void *buffer, size_t size, size_t *retsize);
Result usbFsSyncFile(u64 fileid);
Result usbFsDeleteDir(const char *dirpath);
Result usbFsDeleteFile(const char *filepath);
Result usbFsTruncateFile(u64 fileid, u64 size);
Result usbFsStatFile(u64 fileid, u64 *size, u64 *mode);
Result usbFsStatPath(const char *path, u64 *size, u64 *mode);
Result usbFsStatFilesystem(u64 *totalsize, u64 *freesize);

#endif  /* __USBFS_H__ */
