/*
 * usbfs.c
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include <string.h>

#include "usbfs.h"
#include "service_guard.h"

enum UsbFsServiceCmd {
    UsbFs_Cmd_GetMountStatus = 0,
    UsbFs_Cmd_OpenFile = 1,
    UsbFs_Cmd_CloseFile = 2,
    UsbFs_Cmd_ReadFile = 3,
    UsbFs_Cmd_IsReady = 4,
    UsbFs_Cmd_OpenDir = 5,
    UsbFs_Cmd_CloseDir = 6,
    UsbFs_Cmd_ReadDir = 7,
    UsbFs_Cmd_CreateDir = 8,
    UsbFs_Cmd_SeekFile = 9,
    UsbFs_Cmd_ReadRaw = 10,
    UsbFs_Cmd_WriteFile = 11,
    UsbFs_Cmd_SyncFile = 12,
    UsbFs_Cmd_DeleteDir = 13,
    UsbFs_Cmd_DeleteFile = 14,
    UsbFs_Cmd_TruncateFile = 15,
    UsbFs_Cmd_StatFile = 16,
    UsbFs_Cmd_StatPath = 17,
    UsbFs_Cmd_StatFilesystem = 18,
};

static Service g_usbFsSrv = {0};

NX_GENERATE_SERVICE_GUARD(usbFs);

Result usbFsGetMountStatus(u64 *status)
{
    return serviceDispatchOut(&g_usbFsSrv, UsbFs_Cmd_GetMountStatus, *status);
}

Result usbFsOpenFile(u64 *fileid, const char *filepath, u64 mode)
{
    return serviceDispatchInOut(&g_usbFsSrv, UsbFs_Cmd_OpenFile, mode, *fileid, \
                                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In }, \
                                .buffers = { { filepath, strlen(filepath) + 1 } });
}

Result usbFsCloseFile(u64 fileid)
{
    return serviceDispatchIn(&g_usbFsSrv, UsbFs_Cmd_CloseFile, fileid);
}

Result usbFsReadFile(u64 fileid, void *buffer, size_t size, size_t *retsize)
{
    return serviceDispatchInOut(&g_usbFsSrv, UsbFs_Cmd_ReadFile, fileid, *retsize, \
                                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out }, \
                                .buffers = { { buffer, size } });
}

Result usbFsIsReady()
{
    return serviceDispatch(&g_usbFsSrv, UsbFs_Cmd_IsReady);
}

Result usbFsOpenDir(u64 *dirid, const char *dirpath)
{
    return serviceDispatchOut(&g_usbFsSrv, UsbFs_Cmd_OpenDir, *dirid, \
                              .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In }, \
                              .buffers = { { dirpath, strlen(dirpath) + 1 } });
}

Result usbFsCloseDir(u64 dirid)
{
    return serviceDispatchIn(&g_usbFsSrv, UsbFs_Cmd_CloseDir, dirid);
}

Result usbFsReadDir(u64 dirid, u64 *type, u64 *size, char *name, size_t namemax)
{
    Result rc = 0;
    
    struct {
        u64 type;
        u64 size;
    } out;
    
    rc = serviceDispatchInOut(&g_usbFsSrv, UsbFs_Cmd_ReadDir, dirid, out, \
                              .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out }, \
                              .buffers = { { name, namemax } });
    
    if (R_SUCCEEDED(rc))
    {
        *type = out.type;
        *size = out.size;
    }
    
    return rc;
}

Result usbFsCreateDir(const char *dirpath)
{
    return serviceDispatch(&g_usbFsSrv, UsbFs_Cmd_CreateDir, \
                           .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In }, \
                           .buffers = { { dirpath, strlen(dirpath) + 1 } });
}

Result usbFsSeekFile(u64 fileid, u64 pos, u64 whence, u64 *retpos)
{
    struct {
        u64 fileid;
        u64 pos;
        u64 whence;
    } in = { fileid, pos, whence };
    
    return serviceDispatchInOut(&g_usbFsSrv, UsbFs_Cmd_SeekFile, in, *retpos);
}

Result usbFsReadRaw(u64 sector, u64 sectorcount, void *buffer)
{
    struct {
        u64 sector;
        u64 sectorcount;
    } in = { sector, sectorcount };
    
    return serviceDispatchIn(&g_usbFsSrv, UsbFs_Cmd_ReadRaw, in, \
                             .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out }, \
                             .buffers = { { buffer, 0x200ULL * sectorcount } });
}

Result usbFsWriteFile(u64 fileid, const void *buffer, size_t size, size_t *retsize)
{
    return serviceDispatchInOut(&g_usbFsSrv, UsbFs_Cmd_WriteFile, fileid, *retsize, \
                                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In }, \
                                .buffers = { { buffer, size } });
}

Result usbFsSyncFile(u64 fileid)
{
    return serviceDispatchIn(&g_usbFsSrv, UsbFs_Cmd_SyncFile, fileid);
}

Result usbFsDeleteDir(const char *dirpath)
{
    return serviceDispatch(&g_usbFsSrv, UsbFs_Cmd_DeleteDir, \
                           .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In }, \
                           .buffers = { { dirpath, strlen(dirpath) + 1 } });
}

Result usbFsDeleteFile(const char *filepath)
{
    return serviceDispatch(&g_usbFsSrv, UsbFs_Cmd_DeleteFile, \
                           .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In }, \
                           .buffers = { { filepath, strlen(filepath) + 1 } });
}

Result usbFsTruncateFile(u64 fileid, u64 size)
{
    struct {
        u64 fileid;
        u64 size;
    } in = { fileid, size };
    
    return serviceDispatchIn(&g_usbFsSrv, UsbFs_Cmd_TruncateFile, in);
}

Result usbFsStatFile(u64 fileid, u64 *size, u64 *mode)
{
    Result rc = 0;
    
    struct {
        u64 size;
        u64 mode;
    } out;
    
    rc = serviceDispatchInOut(&g_usbFsSrv, UsbFs_Cmd_StatFile, fileid, out);
    if (R_SUCCEEDED(rc))
    {
        *size = out.size;
        *mode = out.mode;
    }
    
    return rc;
}

Result usbFsStatPath(const char *path, u64 *size, u64 *mode)
{
    Result rc = 0;
    
    struct {
        u64 size;
        u64 mode;
    } out;

    rc = serviceDispatchOut(&g_usbFsSrv, UsbFs_Cmd_StatPath, out, \
                            .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In }, \
                            .buffers = { { path, strlen(path) + 1 } });
    
    if (R_SUCCEEDED(rc))
    {
        *size = out.size;
        *mode = out.mode;
    }
    
    return rc;
}

Result usbFsStatFilesystem(u64 *totalsize, u64 *freesize)
{
    Result rc = 0;
    
    struct {
        u64 totalsize;
        u64 freesize;
    } out;
    
    rc = serviceDispatchOut(&g_usbFsSrv, UsbFs_Cmd_StatFilesystem, out);
    if (R_SUCCEEDED(rc))
    {
        *totalsize = out.totalsize;
        *freesize = out.freesize;
    }
    
    return rc;
}

NX_INLINE Result _usbFsInitialize(void)
{
    return smGetService(&g_usbFsSrv, "usbfs");
}

static void _usbFsCleanup(void)
{
    serviceClose(&g_usbFsSrv);
}
